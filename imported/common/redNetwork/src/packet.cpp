/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "packet.h"
#include "socket.h"

#include "../../redSystem/include/assert.h"

// Need to be defined in the .cpp since passed as a reference to a function.
const red::Network::Packet::Padding red::Network::Packet::HEAD = 0xDEAD;
const red::Network::Packet::Padding red::Network::Packet::FOOT = 0xBEEF;

//////////////////////////////////////////////////////////////////////////
// Packet
//////////////////////////////////////////////////////////////////////////

red::Bool red::Network::Packet::s_debugPadding = true;

red::Network::Packet::Packet()
{
	Clear();
}

void red::Network::Packet::Clear()
{
	m_debugPadding = s_debugPadding;
	red::Memzero( m_buffer, PACKET_BUFFER_SIZE );
}

//////////////////////////////////////////////////////////////////////////
// Incoming Packet
//////////////////////////////////////////////////////////////////////////

red::Network::IncomingPacket::IncomingPacket()
:	m_state( State_ReadingHeader )
,	m_amountReceived( 0 )
,	m_position( 0 )
{
}

red::Network::IncomingPacket::~IncomingPacket()
{

}

void red::Network::IncomingPacket::Reset()
{
	m_state				= State_ReadingHeader;
	m_amountReceived	= 0;
	m_position			= 0;
}

red::Bool red::Network::IncomingPacket::Receive( Socket* source )
{
	if( m_state == State_ReadingHeader )
	{
		ReceiveHeader( source );
	}
	else if( m_state == State_GotHeader || m_state == State_ReadingData )
	{
		m_amountReceived += source->Receive( &m_packet.GetBuffer()[ m_amountReceived ], m_sizeFromBuffer - m_amountReceived );

		if( m_amountReceived == m_sizeFromBuffer )
		{
			m_state = State_Ready;

			return true;
		}
	}

	return false;
}

void red::Network::IncomingPacket::ReceiveHeader( Socket* source )
{
	RED_ASSERT( m_state == State_ReadingHeader );

	Uint16 headerSize = sizeof( Uint16 );
	Uint16 sizePosition = 0;

	// Debug padding
	if( m_amountReceived >= headerSize )
	{
		Packet::Padding headPadding = 0x0000;
		Peek( headPadding, 0 );

		if( headPadding == Packet::HEAD )
		{
			m_packet.SetDebugPadding( true );

			sizePosition += sizeof( Packet::Padding );
			headerSize += sizeof( Packet::Padding );
		}
	}

	// Packet size
	if( m_amountReceived >= headerSize )
	{
		Peek( m_sizeFromBuffer, sizePosition );

		m_position = headerSize;

		RED_ASSERT( m_sizeFromBuffer < Packet::PACKET_BUFFER_SIZE );

		m_state = State_GotHeader;
	}
	else
	{
		m_amountReceived += source->Receive( &m_packet.GetBuffer()[ m_amountReceived ], headerSize - m_amountReceived );
	}
}

//////////////////////////////////////////////////////////////////////////
// Outgoing Packet
//////////////////////////////////////////////////////////////////////////

red::Network::OutgoingPacket::OutgoingPacket()
:	m_ready( false )
,	m_size( 0 )
{
	Initialize();
}

red::Network::OutgoingPacket::~OutgoingPacket()
{

}

void red::Network::OutgoingPacket::Initialize()
{
	Uint16 headerSize = sizeof( Uint16 );

	if( m_packet.HasDebugPadding() )
	{
		headerSize += sizeof( Packet::Padding );
	}

	m_size = headerSize;

	m_ready = true;
}

void red::Network::OutgoingPacket::Clear()
{
	m_ready = false;

	m_packet.Clear();

	Initialize();
}

red::Bool red::Network::OutgoingPacket::Send( OutgoingPacketDestination& destination )
{
	if( m_ready && destination.m_socket->IsReady() )
	{
		// Write header and footer information
		if( destination.m_state == OutgoingPacketDestination::State_Ready )
		{
			Uint16 sizePosition = 0;

			if( m_packet.HasDebugPadding() )
			{
				// Header
				WriteRaw( Packet::HEAD, 0 );
				sizePosition += sizeof( Packet::Padding );

				// Footer
				WriteRaw( Packet::FOOT, m_size );
				m_size += sizeof( Packet::Padding );
			}

			WriteRaw( m_size, sizePosition );

			destination.m_state = OutgoingPacketDestination::State_Sending;
		}

		if( destination.m_state == OutgoingPacketDestination::State_Sending )
		{
			Uint16 sizeToSend = ( m_size - destination.m_sent );

// 			if( sizeToSend > Packet::MTU )
// 			{
// 				sizeToSend = Packet::MTU;
// 			}

			const Uint16 sentBytes = destination.m_socket->Send( &m_packet.GetBuffer()[ destination.m_sent ], sizeToSend );
			if ( sentBytes == 0 && Base::GetLastError() == RED_NET_ERR_SEND_OK )
			{
				return false;
			}

			destination.m_sent += sentBytes;

			if( destination.m_sent == m_size )
			{
				destination.m_state = OutgoingPacketDestination::State_Sent;
			}
		}

		if( destination.m_state == OutgoingPacketDestination::State_Sent )
		{
			return true;
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Floating point packing - ripped wholesale from:
// http://beej.us/guide/bgnet/
// http://beej.us/guide/bgnet/examples/ieee754.c
//////////////////////////////////////////////////////////////////////////

RED_WARNING_PUSH();
RED_DISABLE_WARNING_MSC( 4244 );

uint64_t red::Network::pack754( long double f, unsigned bits, unsigned expbits )
{
	long double fnorm;
	int shift;
	long long sign, exp, significand;
	unsigned significandbits = bits - expbits - 1; // -1 for sign bit

	if (f == 0.0) return 0; // get this special case out of the way

	// check sign and begin normalization
	if (f < 0) { sign = 1; fnorm = -f; }
	else { sign = 0; fnorm = f; }

	// get the normalized form of f and track the exponent
	shift = 0;
	while(fnorm >= 2.0) { fnorm /= 2.0; shift++; }
	while(fnorm < 1.0) { fnorm *= 2.0; shift--; }
	fnorm = fnorm - 1.0;

	// calculate the binary form (non-float) of the significand data
	significand = fnorm * ((1LL<<significandbits) + 0.5f);

	// get the biased exponent
	exp = shift + ((1<<(expbits-1)) - 1); // shift + bias

	// return the final answer
	return (sign<<(bits-1)) | (exp<<(bits-expbits-1)) | significand;
}

long double red::Network::unpack754( uint64_t i, unsigned bits, unsigned expbits )
{
	long double result;
	long long shift;
	unsigned bias;
	unsigned significandbits = bits - expbits - 1; // -1 for sign bit

	if (i == 0) return 0.0;

	// pull the significand
	result = (i&((1LL<<significandbits)-1)); // mask
	result /= (1LL<<significandbits); // convert back to float
	result += 1.0f; // add the one back on

	// deal with the exponent
	bias = (1<<(expbits-1)) - 1;
	shift = ((i>>significandbits)&((1LL<<expbits)-1)) - bias;
	while(shift > 0) { result *= 2.0; shift--; }
	while(shift < 0) { result /= 2.0; shift++; }

	// sign it
	result *= (i>>(bits-1))&1? -1.0: 1.0;

	return result;
}

RED_WARNING_POP();
