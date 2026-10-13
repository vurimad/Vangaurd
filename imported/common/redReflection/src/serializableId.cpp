/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializableId.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

red::Atomic<Uint64> SerializableID::m_nextRuntimeObjectId;

const Uint64 c_invalidSerializableId = ~0ULL;

SerializableID::SerializableID()
	: m_objectId( c_invalidSerializableId )
{
}

SerializableID::SerializableID( Uint64 objectId )
	: m_objectId( objectId )
{
}

SerializableID SerializableID::GenerateRuntimeId()
{
	return SerializableID( m_nextRuntimeObjectId.Increment() );
}

void SerializableID::Copy( const SerializableID& id )
{
	m_objectId = id.m_objectId;
}

bool SerializableID::IsValid() const
{
	return m_objectId != c_invalidSerializableId;
}

//---

const Bool ToString( String& outTxt, const SerializableID& val, const char* customFormat )
{
	RED_UNUSED( customFormat );
	outTxt = ::ToStringDirect( val.Get() );
	return true;
}

const Bool FromString( const String& txt, SerializableID& outVal )
{
	SerializableID::IDType val = 0;
	if ( ::FromString( txt, val ) )
	{
		outVal = SerializableID(val);
		return true;
	}

	return false;
}
