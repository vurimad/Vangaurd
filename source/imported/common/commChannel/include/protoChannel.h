/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "channel.h"

namespace app
{
	//////////////////////////////////////////////////////////////////////////
	// EServiceCommand
	//////////////////////////////////////////////////////////////////////////
	enum class EServiceCommand : Uint8
	{
		None,
		Srv_Success,
		Srv_Failed
	};
}

namespace comm
{
	typedef Uint16 MessageHandle;
	enum class EMessageID : Uint32;

	class COMMCHANNEL_API IProtoResponse
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IProtoResponse() {}
		virtual void Send( const Message& response ) = 0;
		virtual red::UniquePtr< IProtoResponse > Clone() const = 0;
	};

	class COMMCHANNEL_API IProtoMessageListener
	{
	public:
		virtual ~IProtoMessageListener() {}
		virtual bool OnMessage( const MessageSharedPtr& message, comm::IProtoResponse& response ) = 0;
	};

	typedef red::FixedSizeFunction<void( const MessageSharedPtr& )> MessageCallback;

	class COMMCHANNEL_API IProtoChannel : public IChannel
	{
	public:
		virtual void Send( const comm::Message& message ) = 0;
		virtual void SendAck( const comm::Message& message, MessageCallback callback ) = 0;

		virtual const MessageSharedPtr SendAndWait( const comm::Message& message, Uint32 timeoutMs ) = 0;

		virtual void RegisterListener( IProtoMessageListener* handler ) = 0;
		virtual void UnregisterListener( IProtoMessageListener* handler ) = 0;
	};

	class COMMCHANNEL_API IProtoTarget
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		struct MessageEntry
		{
			MessageEntry( const MessageSharedPtr& msg, red::UniquePtr< IProtoResponse > rsp )
				: message( msg )
				, response( std::move( rsp ) )
			{}

			comm::MessageSharedPtr message;
			red::UniquePtr< IProtoResponse > response;
		};

		virtual ~IProtoTarget() {}

		// Returns true if there are any messages pending
		virtual Bool IsPending() const = 0;

		// Pop next pending message with related response interface
		virtual MessageEntry Pop() = 0;

		// Close communication target and unregister from channel
		virtual void Close() = 0;

		// Pass empty filter to accept all messages
		static red::UniquePtr< IProtoTarget > CreateInstance( IProtoChannel& channel, red::DynArray< EMessageID >& filter );
	};
}
