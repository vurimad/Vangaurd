/**
* Copyright (c) 2015-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "protoChannelImpl.h"
#include "protoWriterBinary.h"
#include "protoReaderBinary.h"

namespace comm
{
	ProtoResponseImpl::ProtoResponseImpl( const ProtoResponseImpl& other )
		: m_handle( other.m_handle )
		, m_flags( other.m_flags )
		, m_network( other.m_network )
		, m_connectionID( other.m_connectionID )
	{
	}

	ProtoResponseImpl::ProtoResponseImpl( const ResponseDest& responseDest, Uint16 handle, Uint8 flags )
		: m_handle( handle )
		, m_flags( flags )
		, m_network( responseDest.m_network )
		, m_connectionID( responseDest.m_remoteConnectionID )

	{
	}

	ProtoResponseImpl::ProtoResponseImpl( const ResponseDest& responseDest )
		: m_handle( 0 )
		, m_flags( MessageFlags::FireAndForget )
		, m_network( responseDest.m_network )
		, m_connectionID( responseDest.m_remoteConnectionID )
	{
	}

	void ProtoResponseImpl::Send( const Message& response )
	{
		ProtoWriterBinary writer;

		writer.WriteUint8( m_flags );

		if ( ( m_flags & MessageFlags::Ack ) || m_flags & MessageFlags::NeedsAck )
			writer.WriteUint16( m_handle );

		response.Serialize( writer );

		m_network.Send( m_connectionID, ChannelData ( writer.MoveBuffer() ) );
	}

	red::UniquePtr< IProtoResponse > ProtoResponseImpl::Clone() const
	{
		return red::CreateUniquePtr< ProtoResponseImpl > ( *this );
	}

	ProtoChannelImpl::ProtoChannelImpl( red::UniquePtr< IChannelConnection > connection, const String& name )
		: m_connection( std::move( connection ) )
		, m_name( name )
		, m_listeners( red::PoolEngine() )
		, m_callbacks( red::PoolEngine() )
	{
		m_connection->SetReceiveCallback( [this] ( ChannelData packet, const ResponseDest& responseDest ) -> Bool
		{
			return OnPacket( std::move( packet ), responseDest );
		} );
	}

	ProtoChannelImpl::~ProtoChannelImpl()
	{
		m_connection.Reset();
	}

	bool ProtoChannelImpl::IsOpen() const
	{
		return m_connection->IsOpened();
	}

	bool ProtoChannelImpl::IsConnected() const
	{
		return m_connection->IsConnected();
	}

	void ProtoChannelImpl::Send( const comm::Message& message )
	{
		ProtoWriterBinary writer;
		writer.WriteUint8( MessageFlags::FireAndForget );
		message.Serialize( writer );
		DoSend( writer.MoveBuffer() );
	}

	void ProtoChannelImpl::SendAck( const comm::Message& message, MessageCallback callback )
	{
		red::ScopedLock< red::Mutex > lock( m_callbacksLock );

		MessageHandle handle( m_nextMessageHandle++ );
		m_callbacks.Insert( handle, callback );

		ProtoWriterBinary writer;
		writer.WriteUint8( MessageFlags::NeedsAck );
		writer.WriteUint16( handle );
		message.Serialize( writer );
		DoSend( writer.MoveBuffer() );
	}

	const MessageSharedPtr ProtoChannelImpl::SendAndWait( const comm::Message& message, Uint32 timeoutMs )
	{
		red::ScopedLock< red::Mutex > lock( m_callbacksLock );

		MessageSharedPtr resp;
		red::ConditionVariable cnd;

		MessageCallback callback = [this, &resp, &cnd] ( const MessageSharedPtr& r ) {
			red::ScopedLock< red::Mutex > lock( m_callbacksLock );
			resp = r;
			cnd.WakeAll();
		};

		MessageHandle handle( m_nextMessageHandle++ );
		m_callbacks.Insert( handle, callback );

		ProtoWriterBinary writer;
		writer.WriteUint8( MessageFlags::NeedsAck );
		writer.WriteUint16( handle );
		message.Serialize( writer );

		DoSend( writer.MoveBuffer() );
		cnd.Wait( m_callbacksLock, timeoutMs );

		// In case of timeout callback should be removed
		m_callbacks.Remove( handle );

		return resp;
	}

	void ProtoChannelImpl::RegisterListener( IProtoMessageListener* listener )
	{
		red::ScopedLock<red::Mutex> lock( m_listenersLock );

		RED_ASSERT( listener );
		RED_ASSERT( !m_listeners.Exist( listener ) );

		m_listeners.PushBack( listener );
	}

	void ProtoChannelImpl::UnregisterListener( IProtoMessageListener* listener )
	{
		red::ScopedLock<red::Mutex> lock( m_listenersLock );

		RED_ASSERT( listener );
		//RED_ASSERT( m_listeners.Exist( listener ) );

		m_listeners.Remove( listener );
	}

	Bool ProtoChannelImpl::OnPacket( ChannelData packet, const ResponseDest& responseDest )
	{
		ProtoReaderBinary reader( packet.GetBuffer() );

		Uint8 flags;
		reader.ReadUint8( flags );

		if ( flags & MessageFlags::FireAndForget )
			return OnFireAndForget( reader, responseDest );
		else if ( flags & MessageFlags::NeedsAck )
			return OnNeedsAck( reader, responseDest );
		else if ( flags & MessageFlags::Ack )
			return OnAck( reader );

		RED_LOG_ERROR( "CommChannel: Received invalid message" );
		return false;
	}

	void ProtoChannelImpl::DoSend( red::UniqueBuffer buffer )
	{
		m_connection->Send( ChannelData( std::move( buffer ) ) );
	}

	Bool ProtoChannelImpl::OnFireAndForget( ProtoReaderBinary& reader, const ResponseDest& responseDest )
	{
		ProtoResponseImpl response( responseDest );

		if ( MessageSharedPtr message = Parse( reader ) )
			return DispatchMessageToListeners( message, response );

		return false;
	}

	Bool ProtoChannelImpl::OnNeedsAck( ProtoReaderBinary& reader, const ResponseDest& responseDest )
	{
		Uint16 handle = 0;
		reader.ReadUint16( handle );

		ProtoResponseImpl response( responseDest, handle, MessageFlags::Ack);

		if ( MessageSharedPtr message = Parse( reader ) )
			return DispatchMessageToListeners( message, response );

		return false;
	}

	Bool ProtoChannelImpl::OnAck( ProtoReaderBinary& reader )
	{
		Uint16 handle = 0;
		reader.ReadUint16( handle );

		red::ScopedLock< red::Mutex > lock( m_callbacksLock );

		auto it = m_callbacks.Find( handle );
		if ( it !=  m_callbacks.End() )
		{
			MessageCallback& callback = it.Value();
			callback ( Parse( reader ) );
			m_callbacks.Remove( it );
			return true;
		}

		RED_LOG_ERROR( "CommChannel: Received ACK for a message we didn't send" );
		return false;
	}

	Bool ProtoChannelImpl::DispatchMessageToListeners( MessageSharedPtr& message, IProtoResponse& response )
	{
		red::ScopedLock<red::Mutex> lock( m_listenersLock );

		for ( auto& handler : m_listeners )
		{
			if ( handler->OnMessage( message, response ) )
				return true;
		}

		return false;
	}

	const String& ProtoChannelImpl::GetName() const
	{
		return m_name;
	}
}
