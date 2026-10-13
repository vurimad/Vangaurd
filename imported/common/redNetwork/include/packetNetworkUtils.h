/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/uniqueBuffer.h"

namespace red
{ namespace Network
{ namespace Utils
{
	class Uri
	{
	public:
		// uri format: "scheme://127.0.0.1:50"
		Uri( const String& address, Bool useScheme = false );
		Uri( const Uri& other );

		~Uri();

		// get port
		RED_INLINE const Uint16 GetPort() const { return m_port; }

		// get the original string
		RED_INLINE const String& GetOriginal() const { return m_original; }

		// get scheme part
		RED_INLINE const String& GetScheme() const { return m_scheme; }

		// get host part
		RED_INLINE const String& GetHost() const { return m_host; }

	private:
		void ParseAndSet( const String& str );
		void Clear();

	private:
		Uint16		m_port;

		String		m_original;
		String		m_scheme;
		String		m_host;

		const Bool		m_useScheme : 1; // if should use scheme as address prefix
	};

	class REDNETWORK_API CommAddress
	{
	public:
		CommAddress() = default;
		CommAddress( const String& hostName, Uint16 port );

		RED_INLINE const String& GetHost() const { return m_hostName; }
		RED_INLINE const Uint16 GetPort() const { return m_port; }
		static CommAddress CreateNamedPipeAddress( const String& pipeName );

	private:
		String		m_hostName;
		Uint16		m_port = 0;
	};

	class Packet : public NonCopyable
	{
	public:
		RED_INLINE Packet( Packet&& packet )
			: m_buffer( std::move( packet.m_buffer ) )
		{
		}

		RED_INLINE Packet( red::UniqueBuffer buffer )
			: m_buffer( std::move( buffer ) )
		{}

		RED_INLINE Packet& operator=( Packet&& packet )
		{
			if ( this != &packet )
			{
				m_buffer = std::move( packet.m_buffer );
			}
			return *this;
		}

		RED_INLINE const red::UniqueBuffer& GetBuffer() const { return m_buffer; }
		RED_INLINE red::UniqueBuffer& GetBuffer() { return m_buffer; }
		RED_INLINE const void* Data() const { return m_buffer.Get(); }
		RED_INLINE Uint32 Size() const { return m_buffer.GetSize(); }

	private:
		red::UniqueBuffer m_buffer;
	};

} // red
} // red::Network
} // red::Network::Utils
