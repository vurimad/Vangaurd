/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "dataBuffer.h"

namespace text 
{ 
	class ITextWriter;
	class ITextReader;
}

class SharedDataBufferData;

/// Data buffer with shared content
/// This is mostly used to store data that may be massively duplicated in many different places
/// NOTE 1: This buffer was optimized to waste as little memory as possible in the place it's used, therefore only pointer to the buffer data is stored
/// NOTE 2: For now (W3) the memory for this shit is always coming from the default pool MC_SharedData
/// NOTE 3: To compare the buffers only 64 bit hashes are used. The data content itself is never compared.
class RED_REFLECTION_API SharedDataBuffer
{
public:
	/// Empty default constructor
	RED_FORCE_INLINE SharedDataBuffer()
		: m_data( nullptr )
	{}

	/// Copy constructor, it's very cheap
	SharedDataBuffer( const SharedDataBuffer& other );

	/// Raw constructor - will lookup the hash and try to reuse existing buffer
	SharedDataBuffer( const void* data, const Uint32 size );

	/// Destructor - releases reference to the internal data, does not necessary free the memory
	~SharedDataBuffer();

	/// Assignment
	SharedDataBuffer& operator=( const SharedDataBuffer& other );
	SharedDataBuffer& operator=( SharedDataBuffer&& other );

	/// Comparison
	RED_FORCE_INLINE const Bool operator==( const SharedDataBuffer& other ) const { return m_data == other.m_data; }
	RED_FORCE_INLINE const Bool operator!=( const SharedDataBuffer& other ) const { return m_data != other.m_data; }

	/// Is the buffer empty ?
	RED_FORCE_INLINE const Bool IsEmpty() const { return (m_data == nullptr); }

	/// Get data pointer, NOTE: watch out for the lifetime of this data
	const Uint32 GetSize() const;

	//! Get READ ONLY pointer to data 
	const void* GetData() const;

	//! Get hash of the data, can be used as a key in other places
	const Uint64 GetHash() const;

	//! Clear buffer
	void Clear();

	//! Set new data, note that we only copy it if the hash is not already registered
	void SetData( const void* data, const Uint32 size );

	//! Serialization
	void Serialize( class IFile& file );

	//! Text serialization
	void SerializeToText( class text::ITextWriter& writer ) const;

	//! Text serialization
	void SerializeFromText( class text::ITextReader& reader );

private:
	SharedDataBufferData*		m_data;
};

//------

// We should not allow this data structure to grow big - should be as compact as possible
static_assert( sizeof(SharedDataBuffer) <= 24, "SharedDataBuffer size can't be bigger than 8" );

//------

RTTI_DECLARE_TYPE_NAME( SharedDataBuffer );

//------
