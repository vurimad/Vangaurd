/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "ipcMessageHandler.h"
#include "../../redNetwork/include/socket.h"

namespace red
{
	using TOnConnectedCallback = std::function< void() >;

	struct TcpMessageHandlerParameter
	{
		IpcMessageHandlerParameter ipcParameter;
		red::Network::Address address;
		TOnConnectedCallback onConnectedCallback;
	};

	class RED_CORE_API TcpMessageHandler : public IpcMessageHandler
	{
	public:
		TcpMessageHandler( const TcpMessageHandlerParameter& param, red::Atomic< Bool >& processIsRunning );

		~TcpMessageHandler();

		red::Network::Socket* GetSocket() const;

		virtual void ThreadFunc() override;

	protected:
		Bool ReadMessage();

		void ConnectToServer();

		red::UniquePtr< red::Network::Socket > m_socket;
		TOnConnectedCallback m_onConnectedCallback;
		red::Network::Address m_serverAddress;
	};
}