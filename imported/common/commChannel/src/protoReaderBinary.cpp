/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "protoReaderBinary.h"

ProtoReaderBinary::ProtoReaderBinary( const red::UniqueBuffer& buffer )
	: m_buffer( buffer )
	, m_readPosition( 0 )
{

}

comm::EMessageID ProtoReaderBinary::PeekTypeHash() const
{
	Uint32 readTypeHash;
	red::Memcpy( &readTypeHash, (char*)m_buffer.Get() + m_readPosition, sizeof( readTypeHash ) );
	return static_cast<comm::EMessageID>(readTypeHash);
}

STATIC_CHECK_USE_DECL
void ProtoReaderBinary::Error( const red::AnsiChar* txt, ...) const
{

}

Uint32 ProtoReaderBinary::StartPropsBlock()
{
	Uint32 numberOfProperties = 0;
	Read( &numberOfProperties, sizeof(Uint32) );
	return numberOfProperties;
}

Bool ProtoReaderBinary::ReadFloat(Float& outResult)
{
	return Read( &outResult, sizeof(Float) );
}

Bool ProtoReaderBinary::ReadDouble(Double& outResult)
{
	return Read( &outResult, sizeof(Double) );
}

Bool ProtoReaderBinary::ReadUint8(Uint8& outResult)
{
	return Read( &outResult, sizeof(Uint8) );
}

Bool ProtoReaderBinary::ReadUint16(Uint16& outResult)
{
	return Read( &outResult, sizeof(Uint16) );
}

Bool ProtoReaderBinary::ReadUint32(Uint32& outResult)
{
	return Read( &outResult, sizeof(Uint32) );
}

Bool ProtoReaderBinary::ReadUint64(Uint64& outResult)
{
	return Read( &outResult, sizeof(Uint64) );
}

Bool ProtoReaderBinary::ReadInt8(Int8& outResult)
{
	return Read( &outResult, sizeof(Int8) );
}

Bool ProtoReaderBinary::ReadInt16(Int16& outResult)
{
	return Read( &outResult, sizeof(Int16) );
}

Bool ProtoReaderBinary::ReadInt32(Int32& outResult)
{
	return Read( &outResult, sizeof(Int32) );
}

Bool ProtoReaderBinary::ReadInt64(Int64& outResult)
{
	return Read( &outResult, sizeof(Int64) );
}

Bool ProtoReaderBinary::ReadBool(Bool& outResult)
{
	return Read( &outResult, sizeof(Bool) );
}

Bool ProtoReaderBinary::ReadString( red::String& outResult )
{
	Uint16 length = 0;
	ReadUint16( length );

	outResult.Resize( length );

	Read( outResult.Data(), length );

	return true;
}

Bool ProtoReaderBinary::BeginProperty(Uint32& outNameHash)
{
	Read( &outNameHash, sizeof(Uint32) );
	PushScopedSizeCounter();

	return true;
}

void ProtoReaderBinary::EndProperty()
{
	PopScopedSizeCounter();
}

Bool ProtoReaderBinary::BeginObject(const red::AnsiChar* typeName, const Uint32 typeHash)
{
	Uint32 readTypeHash;
	Read( &readTypeHash, sizeof(Uint32) );

	PushScopedSizeCounter();

	if( readTypeHash != typeHash )
		return false;

	return true;
}

void ProtoReaderBinary::EndObject()
{
	PopScopedSizeCounter();
}

Uint32 ProtoReaderBinary::BeginArray()
{
	Uint32 typeHash;
	Read( &typeHash, sizeof(Uint32) );		// read in the array type hash
	PushScopedSizeCounter();
	Uint32 numElements = 0;
	Read( &numElements, sizeof(Uint32) );

	return numElements;
}

void ProtoReaderBinary::EndArray()
{
	PopScopedSizeCounter();
}

Bool ProtoReaderBinary::Read( void* data, Uint32 size )
{
	RED_FATAL_ASSERT( m_buffer.GetSize() >= m_readPosition + size );
	red::Memcpy( data, ( ( char* ) m_buffer.Get() ) + m_readPosition, size );
	m_readPosition += size;
	return true;
}

void ProtoReaderBinary::PushScopedSizeCounter()
{
	Uint32 bytesForProperty = 0;
	Read( &bytesForProperty, sizeof(Uint32) );
	Uint32 propertyEndPosition = m_readPosition;
	propertyEndPosition += bytesForProperty;
	m_nextEndPosition.Push( propertyEndPosition );
}

void ProtoReaderBinary::PopScopedSizeCounter()
{
	Uint32 endPosition = m_nextEndPosition.Top();
	m_nextEndPosition.Pop();
	m_readPosition = endPosition;
}
