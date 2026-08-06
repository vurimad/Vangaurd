/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "protoSerializationCommon.h"
#include "protoWriterBinary.h"
#include "../redContainers/include/arrayImplUtils.h"
#include "../redNetwork/include/network.h"

ProtoWriterBinary::ProtoWriterBinary()
	: m_writePosition( 0 )
{
	ReallocteBuffer( RED_KILO_BYTE(4) );
}

void ProtoWriterBinary::WriteBool(const Bool value)
{
	Write( &value, sizeof(Bool) );
}

void ProtoWriterBinary::WriteInt64(const Int64 value)
{
	Write( &value, sizeof(Int64) );
}

void ProtoWriterBinary::WriteInt32(const Int32 value)
{
	Write( &value, sizeof(Int32) );
}

void ProtoWriterBinary::WriteInt16(const Int16 value)
{
	Write( &value, sizeof(Int16) );
}

void ProtoWriterBinary::WriteInt8(const Int8 value)
{
	Write( &value, sizeof(Int8) );
}

void ProtoWriterBinary::WriteUint64(const Uint64 value)
{
	Write( &value, sizeof(Uint64) );
}

void ProtoWriterBinary::WriteUint32(const Uint32 value)
{
	Write( &value, sizeof(Uint32) );
}

void ProtoWriterBinary::WriteUint16(const Uint16 value)
{
	Write( &value, sizeof(Uint16) );
}

void ProtoWriterBinary::WriteUint8(const Uint8 value)
{
	Write( &value, sizeof(Uint8) );
}

void ProtoWriterBinary::WriteDouble(const Double value)
{
	Write( &value, sizeof(Double) );
}

void ProtoWriterBinary::WriteFloat(const Float value)
{
	Write( &value, sizeof(Float) );
}

void ProtoWriterBinary::WriteString( const red::String& value )
{
	RED_FATAL_ASSERT( value.Length() < std::numeric_limits< Uint16 >::max(), "This is a very long string" );

	WriteUint16( static_cast<Uint16>( value.Length() ) );
	Write( value.AsChar(), value.Length() );
}

void ProtoWriterBinary::WriteNull()
{
	Uint32 nullValue = 0;
	Write( &nullValue, sizeof(Uint32) );
}

void ProtoWriterBinary::BeginParam(const red::AnsiChar* name, const Uint32 nameHash)
{
	Write( &nameHash, sizeof(Uint32) );

	PushScopedSizeCounter();
}

void ProtoWriterBinary::EndParam()
{
	PopScopedSizeCounter();
}

void ProtoWriterBinary::BeginParams(const Uint32 maxParams)
{
	Uint32 currentPosition = m_writePosition;
	m_paramsPositionStack.Push( currentPosition );	// Push current buffer position on stack
	Write( &maxParams, sizeof(Uint32) );	// Temporarily fill with some data
}

void ProtoWriterBinary::EndParams(Uint32 numSavedParams)
{
	Uint32 counterPosition = m_paramsPositionStack.Top();
	m_paramsPositionStack.Pop();
	Uint32 currentPosition = m_writePosition;
	m_writePosition = counterPosition;
	Write( &numSavedParams, sizeof(Uint32) );
	m_writePosition = currentPosition;
}

void ProtoWriterBinary::BeginObject(const red::AnsiChar* typeName, const Uint32 typeHash)
{
	Write( &typeHash, sizeof(Uint32) );

	PushScopedSizeCounter();
}

void ProtoWriterBinary::EndObject()
{
	PopScopedSizeCounter();
}

void ProtoWriterBinary::BeginArray(const red::AnsiChar* typeName, const Uint32 typeHash, const Uint32 numElements)
{
	Write( &typeHash, sizeof(Uint32) );
	PushScopedSizeCounter();
	Write( &numElements, sizeof(Uint32) );
}

void ProtoWriterBinary::EndArray()
{
	PopScopedSizeCounter();
}

void ProtoWriterBinary::Write( const void* data, Uint32 size )
{
	// resize buffer if too small
	Uint32 desiredSize = m_writePosition + size;
	if ( desiredSize > m_buffer.GetSize() )
		ReallocteBuffer( desiredSize );

	RED_FATAL_ASSERT( m_buffer.GetSize() - m_writePosition >= size );
	red::Memcpy( (char*)m_buffer.Get() + m_writePosition, data, size );
	m_writePosition += size;
}

void ProtoWriterBinary::ReallocteBuffer( Uint32 desiredSize )
{
	Uint32 capacity = red::ArrayImplUtils::CalcResizeCapacity( desiredSize, m_buffer.GetSize() );
	auto buffer = red::Network::CreatePacketBuffer( capacity );
	red::Memcpy( buffer.Get(), m_buffer.Get(), m_writePosition );
	m_buffer = std::move( buffer );
}

void ProtoWriterBinary::PushScopedSizeCounter()
{
	m_propetryPositionStack.Push( m_writePosition );	// Push current buffer position on stack

	Uint32 paramSize = 0;
	Write( &paramSize, sizeof(Uint32) );
}

void ProtoWriterBinary::PopScopedSizeCounter()
{
	Uint32 currentPosition = m_writePosition;
	Uint32 previousPosition = m_propetryPositionStack.Top();
	m_propetryPositionStack.Pop();

	Uint32 positionDifference = (currentPosition - previousPosition - sizeof(Uint32));	// minus param size bytes

	m_writePosition = previousPosition;
	Write( &positionDifference, sizeof(Uint32) );
	m_writePosition = currentPosition;
}

red::UniqueBuffer ProtoWriterBinary::MoveBuffer()
{
	// need to shrink the buffer
	if ( m_writePosition != m_buffer.GetSize() )
	{
		auto buffer = red::Network::CreatePacketBuffer( m_writePosition );
		red::Memcpy( buffer.Get(), m_buffer.Get(), m_writePosition );
		m_buffer = std::move( buffer );
	}

	return std::move( m_buffer );
}
