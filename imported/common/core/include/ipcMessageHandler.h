/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../redContainers/include/string/string.h"
#include "../../redMemory/include/uniquePtr.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redSystem/include/redThreadsThread.h"
#include <functional>

namespace red
{
	struct IpcMessageHeader
	{
		IpcMessageHeader()
			: messageSize( 0 )
			, additionalDataSize( 0 )
		{}

		Uint32 messageSize;
		Uint32 additionalDataSize;
		red::String additionalData;
	};

	using TOnMessageCallback = std::function< void( const IpcMessageHeader& header, const red::String& message ) >;

	struct IpcMessageHandlerParameter
	{
		Uint32 headerSize;
		Uint32 messageSize;
		Uint32 ringBufferCapcity;
		TOnMessageCallback onMessageCallback;
	};

	class RED_CORE_API IpcMessageHandler : public Thread
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		IpcMessageHandler( const AnsiChar* threadName, const IpcMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning );

	private:
		class RingBuffer
		{
		public:
			RingBuffer( Uint32 capacity );
			~RingBuffer();

			RED_INLINE Uint32 Size() const { return m_size; }
			RED_INLINE Uint32 Capacity() const { return m_capacity; }

			// return number of bytes written
			Uint32 Write( const char* data, Uint32 bytes );

			// return number of bytes read
			Uint32 Read( char* data, Uint32 bytes );

		private:
			char* m_data;
			Uint32 m_beginIndex;
			Uint32 m_endIndex;
			Uint32 m_size;
			Uint32 m_capacity;
		};

	protected:
		struct IpcMessage
		{
			IpcMessageHeader header;
			String message;
			Uint32 bytesToRead = 0;
			Bool hasHeader = false;
			Bool hasMessage = false;

			void Reset()
			{
				header = IpcMessageHeader();
				message.Clear();
				bytesToRead = 0;
				hasHeader = false;
				hasMessage = false;
			}
		};

		void OnMessage();
		Bool ParseMessage();

		const Uint32 m_headerSize;
		const Uint32 m_messageSize;
		red::Atomic< Bool >& m_processIsRunning;
		TOnMessageCallback m_onMessageCallback;

		RingBuffer m_ringBuffer;
		IpcMessage m_currentMessage;
	};
}