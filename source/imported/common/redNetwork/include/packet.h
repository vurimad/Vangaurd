/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "network.h"
#include <limits.h>

namespace red
{
	namespace Network
	{
		//////////////////////////////////////////////////////////////////////////
		// Forward declarations
		//////////////////////////////////////////////////////////////////////////
		class Socket;

		//////////////////////////////////////////////////////////////////////////
		// Floating point packing - ripped wholesale from:
		// http://beej.us/guide/bgnet/
		// http://beej.us/guide/bgnet/examples/ieee754.c
		//////////////////////////////////////////////////////////////////////////
		
		RED_WARNING_PUSH();
		RED_DISABLE_WARNING_MSC( 4244 );

#define pack754_32(f) (pack754((f), 32, 8))
#define pack754_64(f) (pack754((f), 64, 11))
#define unpack754_32(i) (unpack754((i), 32, 8))
#define unpack754_64(i) (unpack754((i), 64, 11))

		uint64_t pack754(long double f, unsigned bits, unsigned expbits);
		REDNETWORK_API long double unpack754(uint64_t i, unsigned bits, unsigned expbits);

		RED_WARNING_POP();

		//////////////////////////////////////////////////////////////////////////
		// Incoming Packet
		//////////////////////////////////////////////////////////////////////////
		
		class REDNETWORK_API Packet
		{
		public:
			// Maximum packet size
			static const Uint32	MTU = 1500;
			static const Uint16	PACKET_BUFFER_SIZE = USHRT_MAX;

			// These will be placed in front of each value of the corresponding type for the purpose of debugging, error checking and validation
			typedef Uint16 Padding;

			static const Padding HEAD;
			static const Padding FOOT;

			static const Padding HEAD_ANSICHAR	= 0xAC08;
			static const Padding HEAD_UNICHAR	= 0x9C16;

			static const Padding HEAD_UINT8		= 0x8108;
			static const Padding HEAD_UINT16	= 0x8116;
			static const Padding HEAD_UINT32	= 0x8132;
			static const Padding HEAD_UINT64	= 0x8164;

			static const Padding HEAD_INT8		= 0x7108;
			static const Padding HEAD_INT16		= 0x7116;
			static const Padding HEAD_INT32		= 0x7132;
			static const Padding HEAD_INT64		= 0x7164;

			static const Padding HEAD_FLOAT		= 0x6F32;
			static const Padding HEAD_DOUBLE	= 0x6D64;

			static const Padding HEAD_BOOL		= 0x5B08;

		public:
			Packet();

			void Clear();

		public:

			typedef AnsiChar const* CAnsiString;
			typedef UniChar const* CUniString;

			typedef AnsiChar* AnsiString;
			typedef UniChar* UniString;

			RED_INLINE static Padding GetDebugPadding( const Uint8& )	{ return HEAD_UINT8; }
			RED_INLINE static Padding GetDebugPadding( const Uint16& )	{ return HEAD_UINT16; }
			RED_INLINE static Padding GetDebugPadding( const Uint32& )	{ return HEAD_UINT32; }
			RED_INLINE static Padding GetDebugPadding( const Uint64& )	{ return HEAD_UINT64; }

			RED_INLINE static Padding GetDebugPadding( const Int8& )	{ return HEAD_INT8; }
			RED_INLINE static Padding GetDebugPadding( const Int16& )	{ return HEAD_INT16; }
			RED_INLINE static Padding GetDebugPadding( const Int32& )	{ return HEAD_INT32; }
			RED_INLINE static Padding GetDebugPadding( const Int64& )	{ return HEAD_INT64; }

			RED_INLINE static Padding GetDebugPadding( const Float& )	{ return HEAD_FLOAT; }
			RED_INLINE static Padding GetDebugPadding( const Double& )	{ return HEAD_DOUBLE; }

			RED_INLINE static Padding GetDebugPadding( const CAnsiString& )		{ return HEAD_ANSICHAR; }
			RED_INLINE static Padding GetDebugPadding( const CUniString& )		{ return HEAD_UNICHAR; }

			RED_INLINE static Padding GetDebugPadding( const AnsiString& )		{ return HEAD_ANSICHAR; }
			RED_INLINE static Padding GetDebugPadding( const UniString& )		{ return HEAD_UNICHAR; }

			RED_INLINE static Padding GetDebugPadding( const Bool& )	{ return HEAD_BOOL; }

		public:
			RED_INLINE void SetDebugPadding( Bool debugPadding )	{ m_debugPadding = debugPadding; }
			RED_INLINE Bool HasDebugPadding() const					{ return m_debugPadding; }

		public:
			RED_INLINE Uint8*			GetBuffer()					{ return m_buffer; }
			RED_INLINE const Uint8*		GetBuffer() const			{ return m_buffer; }

		private:
			Uint8 m_buffer[ PACKET_BUFFER_SIZE ];
			Bool m_debugPadding;

			static Bool s_debugPadding;
		};

		//////////////////////////////////////////////////////////////////////////
		// Incoming Packet
		//////////////////////////////////////////////////////////////////////////

		class IncomingPacket
		{
		public:
			enum State
			{
				State_ReadingHeader,
				State_GotHeader,
				State_ReadingData,
				State_Ready,

				State_Max
			};

			IncomingPacket();
			~IncomingPacket();

			void Reset();

			// Grab data from socket
			// Re-entrant, keep calling it until it returns true
			Bool Receive( Socket* source );

		public:

			// Read the data in the buffer at the specified position without affecting the internal buffer position
			template < typename T >
			RED_FORCE_INLINE Bool Peek( T& data, Uint16 position ) const
			{
				// Is there enough space remaining in the buffer for the requested type?
				if( Packet::PACKET_BUFFER_SIZE - position >= sizeof( T ) )
				{
					T intermediary;
					red::Memcpy( &intermediary, &m_packet.GetBuffer()[ position ], sizeof( T ) );
					data = NetworkToHost( intermediary );

					return true;
				}

				return false;
			}

			RED_FORCE_INLINE Bool Peek( Float& data, Uint16 position ) const
			{
				Uint32 unconverted;
				if( Peek( unconverted, position ) )
				{
					data = static_cast< Float >( unpack754_32( unconverted ) );

					return true;
				}

				return false;
			}

			RED_FORCE_INLINE Bool Peek( Double& data, Uint16 position ) const
			{
				Uint64 unconverted;
				if( Peek( unconverted, position ) )
				{
					data = static_cast< Double >( unpack754_64( unconverted ) );

					return true;
				}

				return false;
			}

			// Read the data at the internal buffer position without changing the position
			template < typename T >
			RED_FORCE_INLINE Bool Peek( T& data ) const
			{
				return Peek( data, m_position );
			}

			// Read data and adjust position (no check for debug padding)
			template < typename T >
			RED_FORCE_INLINE Bool ReadRaw( T& data )
			{
				if( !Peek( data ) )
				{
					return false;
				}

				m_position += sizeof( T );

				return true;
			}

			// Read data and adjust position
			template < typename T >
			RED_FORCE_INLINE Bool Read( T& data )
			{
				if( m_packet.HasDebugPadding() )
				{
					Packet::Padding padding = 0x0000;
					if( !Peek( padding ) )
					{
						return false;
					}

					if( padding != m_packet.GetDebugPadding( data ) )
					{
						return false;
					}
					else
					{
						m_position += sizeof( Packet::Padding );
					}
				}

				return ReadRaw( data );
			}

			RED_FORCE_INLINE Bool Read( Bool& data )
			{
				Uint8 intermediary;
				Bool retVal = Read( intermediary );

				if( retVal )
				{
					data = ( intermediary == 1 );
				}

				return retVal;
			}

			// Read array data and adjust position
			// maxElements is the size of the array pointed to by data
			// numElements will be filled with the size of the array stored in the packet
			template < typename T >
			RED_FORCE_INLINE Bool ReadArray( T* data, Uint16 maxElements, Uint16& numElements )
			{
				if( Read( numElements ) )
				{
					if( numElements <= maxElements )
					{
						// We need to read each item individually to make sure the bits are the correct endian
						for( Uint32 i = 0; i < numElements; ++i )
						{
							if( !ReadRaw( data[ i ] ) )
							{
								return false;
							}
						}

						return true;
					}
				}

				return false;
			}

			// Read in a UniChar or AnsiChar array into the character array pointed to by buffer
			// bufferSize is the size of the character array pointed to by buffer
			template < typename T >
			RED_FORCE_INLINE Bool ReadString( T* buffer, Uint16 bufferSize )
			{
				if( m_packet.HasDebugPadding() )
				{
					Packet::Padding padding = 0x0000;
					if( !ReadRaw( padding ) )
					{
						return false;
					}

					if( padding != m_packet.GetDebugPadding( buffer ) )
					{
						return false;
					}
				}

				Uint16 stringLength = 0;

				if( ReadArray( buffer, bufferSize, stringLength ) )
				{
					if( bufferSize > stringLength )
					{
						buffer[ stringLength ] = 0;

						return true;
					}
				}

				return false;
			}

			template < typename T >
			RED_FORCE_INLINE Bool ReadString( T* buffer, Uint32 bufferSize )
			{
				return ReadString( buffer, static_cast< Uint16 >( bufferSize ) );
			}

			// The current internal position in the buffer
			RED_FORCE_INLINE  Uint16 GetPosition() const { return m_position; }

			// Manually adjust the position
			RED_FORCE_INLINE  void SetPosition( Uint16 position ) { m_position = position; }

			// Get the internal packet structure
			RED_FORCE_INLINE  const Packet& GetPacket() const { return m_packet; }

		private:
			// Reentrant, keeps calling recv() until it has enough information to describe the rest of the packet
			void ReceiveHeader( Socket* source );

			Packet m_packet;
			State m_state;

			//Read from socket
			Uint16 m_sizeFromBuffer;
			Uint16 m_amountReceived;

			//Read from packet
			Uint16 m_position;
		};

		//////////////////////////////////////////////////////////////////////////
		// Outgoing Packet Destination
		//////////////////////////////////////////////////////////////////////////
		
		// This is basically a "cookie" class, that should be initialised and passed into
		// OutgoingPacket::Send() until the function returns true
		struct OutgoingPacketDestination
		{
		public:
			enum State
			{
				State_Invalid = 0,
				State_Ready,
				State_Sending,
				State_Sent,

				State_Max
			};

			Socket*			m_socket;
			State			m_state;
			Uint16	m_sent;

			RED_INLINE OutgoingPacketDestination()
			:	m_socket(),
				m_state( State_Invalid ),
				m_sent( 0 )
			{
			}

			RED_INLINE OutgoingPacketDestination( Socket* socket )
			:	m_socket( socket ),
				m_state( State_Ready ),
				m_sent( 0 )
			{
			}

			RED_INLINE void Initialise()
			{
				m_state = State_Invalid;
				m_sent = 0;
			}

			RED_INLINE void Initialise( Socket* socket )
			{
				m_socket = socket;
				m_state = State_Ready;
				m_sent = 0;
			}
		};

		//////////////////////////////////////////////////////////////////////////
		// Outgoing Packet
		//////////////////////////////////////////////////////////////////////////

		class REDNETWORK_API OutgoingPacket
		{
		public:
			OutgoingPacket();
			~OutgoingPacket();

			void Initialize();

			// Clear the packet data entirely for reuse
			void Clear();

			// Send the packet over the specified socket connection
			// Re-entrant, keep calling it until it returns true
			Bool Send( OutgoingPacketDestination& destination );

			// Write data to the specified position without affecting the internal position
			template < typename T >
			RED_FORCE_INLINE void WriteRaw( const T& data, Uint16 position )
			{
				T intermediary = HostToNetwork( data );

				red::Memcpy( &m_packet.GetBuffer()[ position ], &intermediary, sizeof( T ) );
			}

			RED_FORCE_INLINE void WriteRaw( const Float& data, Uint16 position )
			{
				Uint32 converted = static_cast< Uint32 >( pack754_32( data ) );
				WriteRaw( converted, position );
			}

			RED_FORCE_INLINE void WriteRaw( const Double& data, Uint16 position )
			{
				Uint64 converted = static_cast< Uint64 >( pack754_64( data ) );
				WriteRaw( converted, position );
			}

			// Write data to the internal position and adjust (No check for debug padding)
			template < typename T >
			RED_FORCE_INLINE void WriteRaw( const T& data )
			{
				WriteRaw( data, m_size );
				m_size += sizeof( T );
			}

			// Returns amount written
			template < typename T >
			RED_FORCE_INLINE Uint16 Write( const T& data, Uint16 position )
			{
				Uint16 sizeRequired = sizeof( T );

				if( m_packet.HasDebugPadding() )
				{
					sizeRequired += sizeof( Packet::Padding );
				}

				if( Packet::PACKET_BUFFER_SIZE - position >= sizeRequired )
				{
					if( m_packet.HasDebugPadding() )
					{
						WriteRaw( m_packet.GetDebugPadding( data ), position );

						position += sizeof( Packet::Padding );
					}

					WriteRaw( data, position );

					return sizeRequired;
				}

				return 0;
			}

			// Write data to the internal position and adjust
			template < typename T >
			RED_FORCE_INLINE Bool Write( const T& data )
			{
				Uint16 sizeWritten = Write( data, m_size );
				m_size += sizeWritten;

				return sizeWritten > 0;
			}

			RED_FORCE_INLINE Bool Write( const Bool& data )
			{
				Uint8 intermediary = ( data )? 1 : 0;
				return Write( intermediary );
			}

			// Write an array of data to the internal position and adjust
			// numElements is the size of the array pointed to by data
			template < typename T >
			RED_FORCE_INLINE Bool WriteArray( const T* data, Uint16 numElements )
			{
				Uint16 sizeRequired = sizeof( T ) * numElements + sizeof( Uint16 );

				if( m_packet.HasDebugPadding() )
				{
					sizeRequired += sizeof( Packet::Padding );
				}

				if( Packet::PACKET_BUFFER_SIZE - m_size >= sizeRequired )
				{
					Write( numElements );

					for( Uint32 i = 0; i < numElements; ++i )
					{
						WriteRaw( data[ i ] );
					}

					return true;
				}

				return false;
			}

			// Write an AnsiChar or UniChar character array to the internal position and adjust
			// Expects a null terminator (which is not included in the buffer)
			template < typename T >
			RED_FORCE_INLINE Bool WriteString( const T* data )
			{
				if( m_packet.HasDebugPadding() )
				{
					if( Packet::PACKET_BUFFER_SIZE - m_size >= sizeof( Packet::Padding ) )
					{
						WriteRaw( m_packet.GetDebugPadding( data ) );
					}
				}

				Uint16 numElements = static_cast< Uint16 >( red::Strlen( data ) );

				return WriteArray( data, numElements );
			}

			RED_INLINE const Packet& GetPacket() const { return m_packet; }
			RED_INLINE Uint16 GetSize() const { return m_size; }

		protected:
			Packet m_packet;

			Bool m_ready;
			Uint16 m_size;
		};
	}
}
