/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace comm
{

enum class MessageType : Uint8
{
	Register = 0,
	Unregister,
	Question,
	Answer
};

template< size_t capacity >
class PacketWriter
{
public:
	PacketWriter() : m_size( 0 ) {}

	template<typename T>
	void Write( const T& value )
	{
		RED_ASSERT( ( m_size + sizeof( T ) ) < capacity );
		*reinterpret_cast< T * >( &m_data[ m_size ] ) = value;
		m_size += sizeof( T );
	}

	void Write( const void* value, size_t lengthBytes )
	{
		RED_ASSERT( ( m_size + lengthBytes ) < capacity );
		red::Memcpy( &m_data[ m_size ], value, lengthBytes );
		m_size += lengthBytes;
	}

	const Uint8* Data() const { return m_data; }
	size_t Size() const { return m_size; }

private:
	Uint8 m_data[ capacity ];
	size_t m_size;
};

class PacketReader
{
public:
	PacketReader( const void* data, Uint32 size )
		: m_data( ( Uint8 * ) data )
		, m_size( size )
		, m_position( 0 ) 
	{
	}

	template<typename T>
	T Read()
	{
		RED_ASSERT( ( m_position + sizeof( T ) ) < m_size );
		T value = *reinterpret_cast< T* >( &m_data[ m_position ] );
		m_position += sizeof( T );
		return value;
	}

	void Read( void* value, Uint32 lenghtBytes )
	{
		RED_ASSERT( ( m_position + lenghtBytes ) < m_size );
		red::Memcpy( value, &m_data[ m_position ], lenghtBytes );
		m_position += lenghtBytes;
	}

private:
	Uint8* m_data;
	Uint32 m_size;
	Uint32 m_position;
};

}
