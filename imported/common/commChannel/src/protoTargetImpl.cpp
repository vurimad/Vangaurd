/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "protoTargetImpl.h"

namespace comm
{
	red::UniquePtr< IProtoTarget > IProtoTarget::CreateInstance( IProtoChannel& channel, red::DynArray< comm::EMessageID >& filter )
	{
		return red::CreateUniquePtr< prv::ProtoTargetImpl >( channel, filter );
	}

	namespace prv
	{
		//--------------------------------------------------------------

		ProtoTargetImpl::ProtoTargetImpl(comm::IProtoChannel& channel, const red::DynArray<comm::EMessageID>& filter)
			: m_channelListener( m_entries, filter )
			, m_channel( channel )
		{
			m_channel.RegisterListener( &m_channelListener );
		}

		ProtoTargetImpl::~ProtoTargetImpl()
		{
			Close();
		}

		Bool ProtoTargetImpl::IsPending() const
		{
			return !m_entries.Empty();
		}

		ProtoTargetImpl::MessageEntry ProtoTargetImpl::Pop()
		{
			return m_entries.Pop();
		}

		void ProtoTargetImpl::Close()
		{
			m_channel.UnregisterListener( &m_channelListener );
		}

		//--------------------------------------------------------------

		ProtoTargetImpl::ChannelListener::ChannelListener(MessageQueue& queue, const red::DynArray<comm::EMessageID>& filter)
			: m_queue( queue )
			, m_filter( filter )
		{

		}

		bool ProtoTargetImpl::ChannelListener::OnMessage(const comm::MessageSharedPtr& message, comm::IProtoResponse& response)
		{
			Bool pushMessage = false;
			if( m_filter.Empty() )
				pushMessage = true;

			for( comm::EMessageID id : m_filter )
			{
				if( message->IsA( id ) )
				{
					pushMessage = true;
					break;
				}
			}

			if( pushMessage )
			{
				m_queue.Push( MessageEntry( message, response.Clone() ) );
			}

			return pushMessage;
		}

		//--------------------------------------------------------------
	}
}
