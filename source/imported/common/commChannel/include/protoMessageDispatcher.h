/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redThreadsThread.h"

namespace comm {

class IProtoTarget;
class IProtoMessageListener;

//////////////////////////////////////////////////////////////////////////
// A Dispatcher that can be used to transfer messages. Only one listener will handle the message.
//////////////////////////////////////////////////////////////////////////
class COMMCHANNEL_API ProtoMessageDispatcher
{
public:
	ProtoMessageDispatcher( red::UniquePtr< comm::IProtoTarget > messageTargetQueue );

	virtual ~ProtoMessageDispatcher();

	// Add listener to listener queue
	void AddListener( comm::IProtoMessageListener* listener );
	void RemoveListener( comm::IProtoMessageListener* listener );

	// Dispatch pending messages (if there is any)
	void DispatchPendingMessages();

private:
	red::Mutex											m_lock;
	red::DynArray< comm::IProtoMessageListener* >		m_listeners{ red::PoolEngine() };
	red::UniquePtr< comm::IProtoTarget >				m_messageQueue;
};

} // namespace comm {
