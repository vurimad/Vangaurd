/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{
	namespace Network
	{
		class REDNETWORK_API NamedPipe : public red::NonCopyable
		{
			enum class State
			{
				Connecting,
				Connected,
				Closed,
				Error
			};

		public:
			
			enum class PipeType
			{
				Read,
				Writre
			};

			NamedPipe( PipeType type, const String& pipeName, Uint32 bufferSize );
			~NamedPipe();

			Uint32 Send( const void* buffer, Uint32 size );
			Bool Flush();

			// Return true if operation is pending
			Bool Receive( Uint32& bytesRead );

			Bool Connect();
			Bool Open();
			void Close();

			RED_INLINE HANDLE GetEvent() const { return m_overlap.hEvent; }
			RED_INLINE Bool IsConnected() const { return m_state == State::Connected; }
			RED_INLINE Bool IsConnecting() const { return m_state == State::Connecting; }
			RED_INLINE Bool IsClosed() const { return m_state == State::Closed; }
			RED_INLINE Bool IsError() const { return m_state == State::Error; }
			RED_INLINE void* GetReadBuffer() const { return m_readBuffer.Get(); }

			Bool FinishConnecting();
			void DisconnectAndReconnect();

		private:
			void Close( State state );
			void OnError();
			Bool ConnectToNewClient();

			PipeType				m_type;
			String					m_pipeName;

			HANDLE					m_hPipe;
			OVERLAPPED				m_overlap;

			State					m_state;
			Bool					m_pipeOwner;

			Bool					m_isPending;
			red::UniqueBuffer		m_readBuffer;
			Uint32					m_bufferSize;
		};

		class REDNETWORK_API FullDuplexPipe
		{
			RED_USE_MEMORY_POOL( red::PoolEngine );

		public:
			FullDuplexPipe( const String& pipeName, Uint32 bufferSize );
			~FullDuplexPipe();

			RED_INLINE const String& GetPipeName() const	{ return m_pipeName; }
			RED_INLINE void* GetReadBuffer() const			{ return m_readPipe.GetReadBuffer(); }

			Uint32 Send( const void* buffer, Uint32 size );
			Uint32 Receive( red::DynArray< HANDLE >& events );
			Bool Flush();

			Bool Connect();
			Bool Open();
			void Close();

			Bool IsConnected() const;
			Bool IsConnecting() const;
			Bool IsClosed() const;
			Bool IsError() const;
			HANDLE GetReadEvent() const;
			Bool FinishConnecting( red::DynArray< HANDLE >& events );
			Bool FinishConnecting( HANDLE (&events)[2] );

			void DisconnectAndReconnect();

		private:

			NamedPipe		m_readPipe;
			NamedPipe		m_writePipe;

			red::String		m_pipeName;
		};
	}
}
 