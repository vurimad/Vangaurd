/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/// Runtime ID of serializable object
class RED_REFLECTION_API SerializableID
{
public:
	typedef Uint64 IDType;

	SerializableID();

	// Unsafe - use with care
	explicit SerializableID( IDType objectId );

	const Uint32 CalcHash() const;

	IDType Get() const;

	bool IsValid() const;

	// Returns new runtime-only id
	static SerializableID GenerateRuntimeId();

	bool operator==( const SerializableID& rhs ) const;
	bool operator<( const SerializableID& rhs ) const;

private:
	
	// Overwrites id with provided value
	void Copy( const SerializableID& id );

	IDType m_objectId;

	static red::Atomic< IDType > m_nextRuntimeObjectId;
};

// Hashing
RED_INLINE const Uint32 SerializableID::CalcHash() const 
{ 
	return static_cast< Uint32 >( m_objectId ); 
}

// Equality
RED_INLINE bool SerializableID::operator==( const SerializableID& rhs ) const 
{ 
	return m_objectId == rhs.m_objectId; 
}

// Order
RED_INLINE bool SerializableID::operator<( const SerializableID& rhs ) const 
{ 
	return m_objectId < rhs.m_objectId; 
}

RED_INLINE SerializableID::IDType SerializableID::Get() const 
{ 
	return m_objectId; 
}

/// string conversions
extern RED_REFLECTION_API const bool  ToString( red::String& outTxt, const SerializableID& val, const char* customFormat = nullptr );
extern RED_REFLECTION_API const bool  FromString( const red::String& txt, SerializableID& outVal );
