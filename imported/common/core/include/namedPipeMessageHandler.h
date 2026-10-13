/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "ipcMessageHandler.h"
#include "../../redNetwork/include/namedPipe.h"
#include "../../redContainers/include/queue.h"

namespace red
{
	struct NamedPipeMessageHandlerParameter
	{
		IpcMessageHandlerParameter ipcParameter;
		const red::String& namedPipeName;
		Uint32 namedPipeBufferSize;
		Bool isServer;
	};

	class RED_CORE_API NamedPipeMessageHandler final : public IpcMessageHandler
	{
	public:
		NamedPipeMessageHandler( const NamedPipeMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning );

		~NamedPipeMessageHandler();

		red::Network::FullDuplexPipe* GetNamedPipe() const;

		virtual void ThreadFunc() override final;

		void SendMessage( const char* buffer, Uint32 bufferSize );

	private:
		struct Message
		{
			DynArray< char > buffer;
		};

		Bool VerifyClientConnection();
		Bool VerifyServerConnection();

		void ReadMessage();

		Bool ( NamedPipeMessageHandler::* m_verifyConnection )();
		const Uint32 m_namedPipeBufferSize;
		red::UniquePtr< red::Network::FullDuplexPipe > m_namedPipe;
		red::SpinLock m_messagesToSendQueueLock;
		red::Queue< Message > m_messagesToSendQueue{ red::PoolEngine() };
	};
}