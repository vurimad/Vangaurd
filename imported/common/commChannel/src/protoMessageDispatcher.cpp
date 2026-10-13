
#include "build.h"
#include "protoMessageDispatcher.h"
#include "protoChannel.h"

namespace comm {

ProtoMessageDispatcher::ProtoMessageDispatcher( red::UniquePtr< comm::IProtoTarget > messageTargetQueue )
	: m_messageQueue( std::move( messageTargetQueue ) )
{

}

ProtoMessageDispatcher::~ProtoMessageDispatcher()
{

}

void ProtoMessageDispatcher::AddListener( comm::IProtoMessageListener* listener )
{
	red::ScopedLock<red::Mutex> lock( m_lock );

	RED_ASSERT( !m_listeners.Exist( listener ) );
	m_listeners.PushBack( listener );
}

void ProtoMessageDispatcher::RemoveListener( comm::IProtoMessageListener* listener )
{
	red::ScopedLock<red::Mutex> lock( m_lock );

	RED_ASSERT( m_listeners.Exist( listener ) );
	m_listeners.RemoveReorder( listener );
}

void ProtoMessageDispatcher::DispatchPendingMessages()
{
	if ( m_messageQueue->IsPending() )
	{
		red::ScopedLock< red::Mutex > lock( m_lock );

		while ( m_messageQueue->IsPending() )
		{
			auto messageEntry = m_messageQueue->Pop();
			auto& message = messageEntry.message;
			auto& response = messageEntry.response;

			Bool messageHandled = false;

			for( auto listener : m_listeners )
			{
				if( listener->OnMessage( message, *response ) )
				{
					messageHandled = true;
					break;
				}
			}

			if( !messageHandled )
			{
				comm::Exception ex;
				ex.text = "Message not handled exception.";
				response->Send( ex );
			}
		}
	}
}

} // namespace comm {
