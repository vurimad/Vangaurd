/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "ipcMessageHandler.h"

namespace red
{
	IpcMessageHandler::IpcMessageHandler( const AnsiChar* threadName, const IpcMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning )
		: Thread( threadName, { RED_KILO_BYTE( 128 ) } )
		, m_headerSize( param.headerSize )
		, m_messageSize( param.messageSize )
		, m_processIsRunning(processIsRunning)
		, m_onMessageCallback( param.onMessageCallback )
		, m_ringBuffer( param.ringBufferCapcity )
	{
	}

	void IpcMessageHandler::OnMessage()
	{
		RED_ASSERT( m_currentMessage.bytesToRead == 0, "Message is incomplete" );
		RED_ASSERT( m_currentMessage.header.messageSize == m_currentMessage.message.Length(), "Inconsistent message meta data" );

		m_onMessageCallback( m_currentMessage.header, m_currentMessage.message );

		m_currentMessage.Reset();
	}

	Bool IpcMessageHandler::ParseMessage()
	{
		if ( !m_currentMessage.hasHeader )
		{
			if ( m_ringBuffer.Size() >= m_headerSize )
			{
				// read header
				m_ringBuffer.Read( reinterpret_cast< char* >( &m_currentMessage.header.messageSize ), m_messageSize );

				RED_ASSERT( m_currentMessage.header.messageSize < m_ringBuffer.Capacity(), "Message size can not be bigger then buffer capacity" );

				if ( m_headerSize > m_messageSize )
				{
					m_currentMessage.header.additionalDataSize = m_headerSize - m_messageSize;
					m_currentMessage.header.additionalData.Resize( m_currentMessage.header.additionalDataSize );
					m_ringBuffer.Read( reinterpret_cast< char* >( &m_currentMessage.header.additionalData ), m_currentMessage.header.additionalDataSize );
				}

				m_currentMessage.hasHeader = true;
			}
		}

		if ( m_currentMessage.hasHeader )
		{
			if ( m_currentMessage.header.messageSize == 0 )
			{
				m_currentMessage.hasMessage = true;
				return true;
			}

			if ( m_ringBuffer.Size() >= m_currentMessage.header.messageSize )
			{
				// read message
				m_currentMessage.message.Resize( m_currentMessage.header.messageSize );
				m_ringBuffer.Read( &m_currentMessage.message[0], m_currentMessage.header.messageSize );
				m_currentMessage.hasMessage = true;
				return true;
			}
		}

		return false;
	}

	IpcMessageHandler::RingBuffer::RingBuffer( Uint32 capacity )
		: m_beginIndex( 0 )
		, m_endIndex( 0 )
		, m_size( 0 )
		, m_capacity( capacity )
	{
		m_data = RED_NEW_ARRAY( char, m_capacity, red::PoolDebug() );
	}

	IpcMessageHandler::RingBuffer::~RingBuffer()
	{
		RED_DELETE_ARRAY( m_data, m_capacity, red::PoolDebug() );
	}

	Uint32 IpcMessageHandler::RingBuffer::Write( const char* data, Uint32 bytes )
	{
		if ( bytes == 0 )
		{
			return 0;
		}

		const Uint32 capacity = m_capacity;
		const Uint32 bytesToWrite = std::min( bytes, capacity - m_size );

		if ( bytesToWrite <= capacity - m_endIndex )
		{
			Memcpy( m_data + m_endIndex, data, bytesToWrite );
			m_endIndex += bytesToWrite;
			if ( m_endIndex == capacity )
			{
				m_endIndex = 0;
			}
		}
		else
		{
			Uint32 size1 = capacity - m_endIndex;
			Memcpy( m_data + m_endIndex, data, size1 );
			Uint32 size2 = bytesToWrite - size1;
			Memcpy( m_data, data + size1, size2 );
			m_endIndex = size2;
		}

		m_size += bytesToWrite;
		return bytesToWrite;
	}

	Uint32 IpcMessageHandler::RingBuffer::Read( char* data, Uint32 bytes )
	{
		if ( bytes == 0 )
		{
			return 0;
		}

		const Uint32 capacity = m_capacity;
		const Uint32 bytesToRead = std::min( bytes, m_size );

		if ( bytesToRead <= capacity - m_beginIndex )
		{
			Memcpy( data, m_data + m_beginIndex, bytesToRead );
			m_beginIndex += bytesToRead;
			if ( m_beginIndex == capacity )
			{
				m_beginIndex = 0;
			}
		}
		else
		{
			Uint32 size1 = capacity - m_beginIndex;
			Memcpy( data, m_data + m_beginIndex, size1 );
			Uint32 size2 = bytesToRead - size1;
			Memcpy( data + size1, m_data, size2 );
			m_beginIndex = size2;
		}

		m_size -= bytesToRead;
		return bytesToRead;
	};
}