/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/uniqueBuffer.h"

#include "channel.h"

namespace comm
{
	class COMMCHANNEL_API IRawResponse
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );
	public:
		virtual ~IRawResponse() {}
		virtual void Send( red::UniqueBuffer buffer ) = 0;
	};

	using ResponseHandle = red::UniquePtr< comm::IRawResponse >;

	class COMMCHANNEL_API IRawMessageListener
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );
	public:
		virtual ~IRawMessageListener() {}
		virtual Bool OnMessage( red::UniqueBuffer& buffer, ResponseHandle& response ) = 0;
	};

	class COMMCHANNEL_API IRawChannel : public IChannel
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolEngine );
	public:
		virtual void Send( red::UniqueBuffer buffer ) = 0;

		virtual void RegisterListener( IRawMessageListener* listener ) = 0;
		virtual void UnregisterListener( IRawMessageListener* listener ) = 0;
	};
}
