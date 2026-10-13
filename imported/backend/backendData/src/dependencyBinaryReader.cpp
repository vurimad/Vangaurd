/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyBinaryReader.h"

#include "dataPlatform.h"
#include "dependencyTypes.h"
#include "dependencyTypesImpl.h"

#include "../../../common/archive/include/common.h" // For archive::FileHash

namespace dependency
{
namespace prv
{

//------------------------------------------------------------------------------

StringsUnmapper::StringsUnmapper( const red::ArraySpan< const fileformat::IndexEntry >& indexEntries, red::BlobView& stringsData )
	: m_index( indexEntries )
	, m_strings( stringsData )
{
}

red::StringView StringsUnmapper::Find( Uint32 index ) const
{
	red::StringView result;
	if ( index != INVALID_INDEX && index < m_strings.Size() )
	{
		result = red::StringView( m_strings.Pointer< const char >( index ) );
	}
	return result;
}

CName StringsUnmapper::GetName( Uint32 index ) const
{
	auto sv = Find( index );
	if ( sv.Empty() )
	{
		return CName::NONE();
	}
	return RED_NAME( sv );
}

red::String StringsUnmapper::GetString( Uint32 index ) const
{
	return Find( index ).ToString();
}

res::ResourcePath StringsUnmapper::GetResourcePath( Uint32 index ) const
{
	res::ResourcePath result;
	auto sv = Find( index );
	if ( !sv.Empty() )
	{
		result = res::ResourcePath::Build( sv );
	}
	return result;
}

//------------------------------------------------------------------------------

BinaryReadContext::BinaryReadContext( red::BlobView bufferView )
	: m_bufferView( bufferView )
	, m_offset( 0 )
{}

template < typename T >
void BinaryReadContext::Read( T& element )
{
	element = m_bufferView.As< const T >( m_offset );
	m_offset += sizeof( T );
}

void BinaryReadContext::Read( Uint8* ptr, Uint32 size )
{
	RED_FATAL_ASSERT( m_offset + size < m_bufferView.Size(), "Overflow when reading %u bytes from buffer", size );
	const Uint8* src = m_bufferView.Pointer< const Uint8 >( m_offset );
	red::Memcpy( ptr, src, size );
	m_offset += size;
}

class BinaryReader : public Reader
{
public:
	BinaryReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context );
	virtual ~BinaryReader();

	virtual bool HasErrors() const override;

	virtual bool HasValue( const red::StringView& key ) const override;

	virtual ReaderScope StartScope( const red::StringView& key ) override;
	virtual ReaderArrayScope StartArray( const red::StringView& key ) override;

	virtual void ReadName( const red::StringView& key, CName& name ) override;
	virtual void ReadBool( const red::StringView& key, bool& value ) override;
	virtual void ReadNumber( const red::StringView& key, Int32& number ) override;
	virtual void ReadNumber( const red::StringView& key, Uint32& number ) override;
	virtual void ReadNumber( const red::StringView& key, Int64& number ) override;
	virtual void ReadNumber( const red::StringView& key, Uint64& number ) override;
	virtual void ReadFloat( const red::StringView& key, float& number ) override;
	virtual void ReadString( const red::StringView& key, red::String& value ) override;

	virtual void ReadPath( const red::StringView& key, res::ResourcePath& value ) override;
	virtual void ReadTimestamp( const red::StringView& key, Timestamp& timestamp ) override;
	virtual void ReadHash( const red::StringView& key, Hash& hash ) override;

private:
	const StringsUnmapper& m_stringsUnmapper;
	BinaryReadContext& m_context;
};

class BinaryArrayReader : public ReaderArray
{
public:
	BinaryArrayReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context );
	virtual ~BinaryArrayReader();

	virtual Uint32 Size() const override;
	virtual ReaderScope GetScope( Uint32 index ) const override;

	virtual void ReadName( Uint32 index, CName& name ) override;
	virtual void ReadNumber( Uint32 index, Int32& number ) override;
	virtual void ReadNumber( Uint32 index, Uint32& number ) override;
	virtual void ReadNumber( Uint32 index, Int64& number ) override;
	virtual void ReadNumber( Uint32 index, Uint64& number ) override;
	virtual void ReadFloat( Uint32 index, float& number ) override;
	virtual void ReadString( Uint32 index, red::String& value ) override;
	virtual void ReadPath( Uint32 index, res::ResourcePath& value ) override;

private:
	const StringsUnmapper& m_stringsUnmapper;
	BinaryReadContext& m_context;
	Uint32 m_elements;
};

ReaderScope CreateBinaryReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context )
{
	return red::CreateUniquePtr< BinaryReader >( stringsUnmapper, context );
}

BinaryReader::BinaryReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context )
	: m_stringsUnmapper( stringsUnmapper )
	, m_context( context )
{
}

BinaryReader::~BinaryReader()
{
}

bool BinaryReader::HasErrors() const
{
	return false;
}

bool BinaryReader::HasValue( const red::StringView& key ) const
{
	return true;
}

ReaderScope BinaryReader::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< BinaryReader >( m_stringsUnmapper, m_context );
}

ReaderArrayScope BinaryReader::StartArray( const red::StringView& key )
{
	return red::CreateUniquePtr< BinaryArrayReader >( m_stringsUnmapper, m_context );
}

void BinaryReader::ReadName( const red::StringView& key, CName& name )
{
	Uint32 index = StringsUnmapper::INVALID_INDEX;
	m_context.Read( index );
	name = m_stringsUnmapper.GetName( index );
}

void BinaryReader::ReadBool( const red::StringView& key, bool& value )
{
	Uint8 val = 0;
	m_context.Read( val );
	value = val != 0;
}

void BinaryReader::ReadNumber( const red::StringView& key, Int32& number )
{
	m_context.Read( number );
}

void BinaryReader::ReadNumber( const red::StringView& key, Uint32& number )
{
	m_context.Read( number );
}

void BinaryReader::ReadNumber( const red::StringView& key, Int64& number )
{
	m_context.Read( number );
}

void BinaryReader::ReadNumber( const red::StringView& key, Uint64& number )
{
	m_context.Read( number );
}

void BinaryReader::ReadFloat( const red::StringView& key, float& number )
{
	m_context.Read( number );
}

void BinaryReader::ReadString( const red::StringView& key, red::String& value )
{
	Uint32 index = StringsUnmapper::INVALID_INDEX;
	m_context.Read( index );
	value = m_stringsUnmapper.GetString( index );
}

void BinaryReader::ReadPath( const red::StringView& key, res::ResourcePath& value )
{
	Uint32 index = StringsUnmapper::INVALID_INDEX;
	m_context.Read( index );
	value = m_stringsUnmapper.GetResourcePath( index );
}

void BinaryReader::ReadTimestamp( const red::StringView& key, Timestamp& timestamp )
{
	m_context.Read( timestamp );
}

void BinaryReader::ReadHash( const red::StringView& key, Hash& hash )
{
	archive::FileHash fileHash;
	m_context.Read( fileHash.value, archive::FileHash::NUM_BYTES );
	hash = Hash::FromFileHash( fileHash );
}

BinaryArrayReader::BinaryArrayReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context )
	: m_stringsUnmapper( stringsUnmapper )
	, m_context( context )
	, m_elements( 0 )
{
	m_context.Read( m_elements );
}

BinaryArrayReader::~BinaryArrayReader()
{
}

Uint32 BinaryArrayReader::Size() const
{
	return m_elements;
}

ReaderScope BinaryArrayReader::GetScope( Uint32 index ) const
{
	RED_FATAL_ASSERT( index < m_elements );
	return red::CreateUniquePtr< BinaryReader >( m_stringsUnmapper, m_context );
}

void BinaryArrayReader::ReadName( Uint32 index, CName& name )
{
	RED_FATAL_ASSERT( index < m_elements );
	Uint32 offset = StringsUnmapper::INVALID_INDEX;
	m_context.Read( offset );
	name = m_stringsUnmapper.GetName( offset );
}

void BinaryArrayReader::ReadNumber( Uint32 index, Int32& number )
{
	RED_FATAL_ASSERT( index < m_elements );
	m_context.Read( number );
}

void BinaryArrayReader::ReadNumber( Uint32 index, Uint32& number )
{
	RED_FATAL_ASSERT( index < m_elements );
	m_context.Read( number );
}

void BinaryArrayReader::ReadNumber( Uint32 index, Int64& number )
{
	RED_FATAL_ASSERT( index < m_elements );
	m_context.Read( number );
}

void BinaryArrayReader::ReadNumber( Uint32 index, Uint64& number )
{
	RED_FATAL_ASSERT( index < m_elements );
	m_context.Read( number );
}

void BinaryArrayReader::ReadFloat( Uint32 index, float& number )
{
	RED_FATAL_ASSERT( index < m_elements );
	m_context.Read( number );
}

void BinaryArrayReader::ReadString( Uint32 index, red::String& value )
{
	RED_FATAL_ASSERT( index < m_elements );
	Uint32 offset = StringsUnmapper::INVALID_INDEX;
	m_context.Read( offset );
	value = m_stringsUnmapper.GetString( offset );
}

void BinaryArrayReader::ReadPath( Uint32 index, res::ResourcePath& value )
{
	RED_FATAL_ASSERT( index < m_elements );
	Uint32 offset = StringsUnmapper::INVALID_INDEX;
	m_context.Read( offset );
	value = m_stringsUnmapper.GetResourcePath( offset );
}


} // prv
} // dependency
