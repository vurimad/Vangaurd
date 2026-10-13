/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace comm
{
	class COMMCHANNEL_API IChannel
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IChannel() {}

		virtual bool IsOpen() const = 0;
		virtual bool IsConnected() const = 0;
		virtual const red::String& GetName() const = 0;
	};
}
