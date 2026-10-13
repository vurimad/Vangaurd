/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packetNetworkUtils.h"
#include "../../redSystem/include/assert.h"

namespace red
{
	namespace Network
	{
		namespace Utils
		{
			//////////////////////////////////////////////////////////////////////////
			// CUri
			//////////////////////////////////////////////////////////////////////////
			Uri::Uri( const String& address, Bool useScheme /* false */ ) 
				: m_port( -1 )
				, m_original( nullptr )
				, m_scheme( nullptr )
				, m_host( nullptr )
				, m_useScheme( useScheme )
			{
				ParseAndSet( address );
			}

			Uri::Uri( const Uri& other )
				: m_useScheme( other.m_useScheme )
			{
				ParseAndSet( other.m_original );
			}

			Uri::~Uri()
			{
				Clear();
			}

			void Uri::ParseAndSet( const String& str )
			{				
				enum class ParsingStage : Uint8
				{
					Scheme,
					Host,
					Port
				};

				Clear();
				m_port = -1; // default port if nothing was provided
				m_original = str;
								
				red::AnsiChar c; // currently processed character
				ParsingStage stage = ( m_useScheme ) ? ParsingStage::Scheme : ParsingStage::Host; // current processing stage
				String buffer; // helper buffer

				// parse
				for ( Uint32 i = 0; i < m_original.Length(); ++i )
				{
					c = str[ i ];

					switch ( stage )
					{
					case ParsingStage::Scheme:
						// parse scheme
						if ( c == ':' )
						{							
							i += 2; // after scheme there should be "//", so move by two characters
							RED_ASSERT( i <  m_original.Length(), "Invalid URI address format, scheme must be followed by '://'" );
							RED_ASSERT( str[ i - 1 ] == '/', "Invalid URI address format, missing '//'" );
							RED_ASSERT( str[ i ] == '/', "Invalid URI address format, missing '/'" );

							// set scheme value
							m_scheme = buffer;

							// parse host
							stage = ParsingStage::Host;
						}
						else
						{
							// normal character, append it to the buffer
							buffer += c;
						}
						break;
					case ParsingStage::Host:
						if ( c == ':' )
						{
							m_host = buffer;
							buffer.Clear();

							stage = ParsingStage::Port;
						}
						else
						{
							buffer += c;
						}

						break;
					case ParsingStage::Port:
						// everything that follows host
						buffer += c;
						break;
					default:
						break;
					}
				}

				// filled buffer means that the last value was provided
				if ( !buffer.Empty() )
				{
					// the last not assigned value can be port or host
					if ( stage == ParsingStage::Port )
					{
						m_port = ( Uint16 ) std::atoi( buffer.AsChar() );
					}
					else
					{
						m_host = buffer;
					}
					
					buffer.Clear();
				}
			}

			void Uri::Clear()
			{
				m_original.Clear();
				m_scheme.Clear();
				m_host.Clear();
			}

			//////////////////////////////////////////////////////////////////////////
			// CommAddress
			//////////////////////////////////////////////////////////////////////////

			CommAddress::CommAddress( const String& hostName, Uint16 port )
				: m_hostName( hostName )
				, m_port( port )
			{
			}

			CommAddress CommAddress::CreateNamedPipeAddress( const String& pipeName )
			{
				Uri uri( "\\\\.\\pipe\\" + pipeName, false );
				return CommAddress( uri.GetHost(), uri.GetPort() );
			}
		}
	}
}