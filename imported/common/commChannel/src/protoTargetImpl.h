/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "protoChannel.h"

namespace comm
{
	namespace prv
	{
		class ProtoTargetImpl : public IProtoTarget
		{
		public:
			ProtoTargetImpl( comm::IProtoChannel& channel, const red::DynArray<comm::EMessageID>& filter );
			~ProtoTargetImpl();

			virtual Bool IsPending() const override;
			virtual MessageEntry Pop() override;
			virtual void Close() override;

		private:
			class MessageQueue
			{
				constexpr const static Uint32 InitialCapacity = 100;

			public:
				RED_INLINE MessageQueue()
					: m_isEmpty( true )
				{
					// reserve initial queue capacity
					m_queue.Reserve( InitialCapacity );
				}

				RED_INLINE Bool Empty() const
				{
					return m_isEmpty.GetValue();
				}

				RED_INLINE void Push( MessageEntry elem )
				{
					red::ScopedLock<red::Mutex> lock( m_lock );
					m_queue.Push( std::move( elem ) );
					m_isEmpty.SetValue( false );
				}

				RED_INLINE MessageEntry Pop()
				{
					red::ScopedLock<red::Mutex> lock( m_lock );
					MessageEntry elem = m_queue.Pop();
					m_isEmpty.SetValue( m_queue.Empty() );
					return elem;
				}

			private:
				red::Mutex m_lock;
				red::Atomic< Bool > m_isEmpty;
				red::Queue< MessageEntry > m_queue{ red::PoolEngine() };
			};

			class ChannelListener : public IProtoMessageListener
			{
			public:
				ChannelListener( MessageQueue& queue, const red::DynArray<comm::EMessageID>& filter );
				virtual bool OnMessage(const MessageSharedPtr& message, comm::IProtoResponse& response);

			private:
				MessageQueue& m_queue;
				red::DynArray<comm::EMessageID> m_filter;
			};

			MessageQueue m_entries;
			ChannelListener m_channelListener;
			comm::IProtoChannel& m_channel;
		};
	}
}

