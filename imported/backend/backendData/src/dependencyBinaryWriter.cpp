/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyBinaryWriter.h"

#include "dataPlatform.h"
#include "dependencyTypes.h"
#include "dependencyTypesImpl.h"

#include "../../../common/archive/include/common.h" // For archive::FileHash

namespace dependency
{
namespace prv
{

//------------------------------------------------------------------------------

class MappingWriter : public Writer
{
public:
	explicit MappingWriter( MappingContext& context );
	virtual ~MappingWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;
	virtual WriterArrayScope StartArray( const red::StringView& key, Uint32 elements ) override;

	virtual void WriteName( const red::StringView& key, const CName name ) override;
	virtual void WriteBool( const red::StringView& key, bool value ) override;
	virtual void WriteNumber( const red::StringView& key, Int32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Int64 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint64 number ) override;
	virtual void WriteFloat( const red::StringView& key, float number ) override;
	virtual void WriteString( const red::StringView& key, const red::StringView& value ) override;

	virtual void WritePath( const red::StringView& key, const res::ResourcePath& value ) override;
	virtual void WriteTimestamp( const red::StringView& key, const Timestamp timestamp ) override;
	virtual void WriteHash( const red::StringView& key, const Hash& hash ) override;

private:
	MappingContext& m_context;
};

class MappingArrayWriter : public WriterArray
{
public:
	explicit MappingArrayWriter( MappingContext& context );
	virtual ~MappingArrayWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;

	virtual void WriteName( const CName name ) override;
	virtual void WriteNumber( Int32 number ) override;
	virtual void WriteNumber( Uint32 number ) override;
	virtual void WriteNumber( Int64 number ) override;
	virtual void WriteNumber( Uint64 number ) override;
	virtual void WriteFloat( float number ) override;
	virtual void WriteString( const red::StringView& value ) override;
	virtual void WritePath( const res::ResourcePath& value ) override;

private:
	MappingContext& m_context;
};

WriterScope CreateMappingWriter( MappingContext& context )
{
	return red::CreateUniquePtr< MappingWriter >( context );
}

MappingWriter::MappingWriter( MappingContext& context )
	: m_context( context )
{}

MappingWriter::~MappingWriter()
{}

WriterScope MappingWriter::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< MappingWriter >( m_context );
}

WriterArrayScope MappingWriter::StartArray( const red::StringView& key, Uint32 elements )
{
	m_context.m_memoryRequired += sizeof(Uint32); // Storage for array element count
	return red::CreateUniquePtr< MappingArrayWriter >( m_context );
}

void MappingWriter::WriteName( const red::StringView& key, const CName name )
{
	m_context.m_names.PushBack( name );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingWriter::WriteString( const red::StringView& key, const red::StringView& value )
{
	m_context.m_strings.PushBack( value.ToString() );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingWriter::WritePath( const red::StringView& key, const res::ResourcePath& value )
{
	m_context.m_paths.PushBack( value );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingWriter::WriteBool( const red::StringView& key, bool value )
{
	m_context.m_memoryRequired += sizeof(Uint8);
}

void MappingWriter::WriteNumber( const red::StringView& key, Int32 number )
{
	m_context.m_memoryRequired += sizeof(Int32);
}

void MappingWriter::WriteNumber( const red::StringView& key, Uint32 number )
{
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingWriter::WriteNumber( const red::StringView& key, Int64 number )
{
	m_context.m_memoryRequired += sizeof(Int64);
}

void MappingWriter::WriteNumber( const red::StringView& key, Uint64 number )
{
	m_context.m_memoryRequired += sizeof(Uint64);
}

void MappingWriter::WriteFloat( const red::StringView& key, float number )
{
	m_context.m_memoryRequired += sizeof(float);
}

void MappingWriter::WriteTimestamp( const red::StringView& key, const Timestamp timestamp )
{
	m_context.m_memoryRequired += sizeof(Timestamp);
}

void MappingWriter::WriteHash( const red::StringView& key, const Hash& hash )
{
	m_context.m_memoryRequired += archive::FileHash::NUM_BYTES;
}

MappingArrayWriter::MappingArrayWriter( MappingContext& context )
	: m_context( context )
{}

MappingArrayWriter::~MappingArrayWriter()
{}

WriterScope MappingArrayWriter::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< MappingWriter >( m_context );
}

void MappingArrayWriter::WriteName( const CName name )
{
	m_context.m_names.PushBack( name );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingArrayWriter::WriteString( const red::StringView& value )
{
	m_context.m_strings.PushBack( value.ToString() );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingArrayWriter::WritePath( const res::ResourcePath& value )
{
	m_context.m_paths.PushBack( value );
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingArrayWriter::WriteNumber( Int32 number )
{
	m_context.m_memoryRequired += sizeof(Int32);
}

void MappingArrayWriter::WriteNumber( Uint32 number )
{
	m_context.m_memoryRequired += sizeof(Uint32);
}

void MappingArrayWriter::WriteNumber( Int64 number )
{
	m_context.m_memoryRequired += sizeof(Int64);
}

void MappingArrayWriter::WriteNumber( Uint64 number )
{
	m_context.m_memoryRequired += sizeof(Uint64);
}

void MappingArrayWriter::WriteFloat( float number )
{
	m_context.m_memoryRequired += sizeof(float);
}


//------------------------------------------------------------------------------


StringsMapping::Key::Key( const CName name )
	: m_string( name.AsStringView() )
{
}

StringsMapping::Key::Key( const red::StringView& string )
	: m_string( string )
{
}

StringsMapping::Key::Key( const res::ResourcePath& path )
	: m_string( path.ToStringView() )
{
}

StringsMapping::Key::Key( const char* ptr, Uint32 length )
	: m_string( ptr, length )
{
}

bool StringsMapping::Key::operator==( const Key& other ) const
{
	return m_string == other.m_string;
}

bool StringsMapping::Key::operator<( const Key& other ) const
{
	return m_string < other.m_string;
}

Uint32 StringsMapping::Key::CalcHash() const
{
	return red::CalculateHash32( m_string.Data(), m_string.Length() );
}

Uint32 StringsMapping::Key::Length() const
{
	return m_string.Length();
}

const red::StringView& StringsMapping::Key::AsStringView() const
{
	return m_string;
}


void StringsMapping::AddCollectedStrings( const red::DynArray< MappingContext >& contexts )
{
	// Count total number of strings collected
	Uint32 totalNames = 0;
	Uint32 totalStrings = 0;
	Uint32 totalPaths = 0;
	for ( const auto& context : contexts )
	{
		totalNames += context.m_names.Size();
		totalStrings += context.m_strings.Size();
		totalPaths += context.m_paths.Size();
	}
	RED_LOG_TRACE( "BinaryDependencyDatabase: Collected %u names, %u strings, %u resource paths", totalNames, totalStrings, totalPaths );

	Uint64 itemCount = totalNames + totalStrings + totalPaths;
	RED_FATAL_ASSERT( itemCount < std::numeric_limits< Uint32 >::max(), "Attempting to map too many strings, need to re-evaluate this code! (%llu strings)", itemCount );

	// Gather the unique ones and figure out how much memory they will take
	red::HashSet< Key > uniqueItems;
	uniqueItems.Reserve( static_cast< Uint32 >( itemCount ) );

	Uint64 bufferMemory = 0;
	for ( const auto& context : contexts )
	{
		for ( const auto& name : context.m_names )
		{
			auto key = Key( name );
			auto result = uniqueItems.Insert( key );
			if ( result.IsSuccessful() )
			{
				bufferMemory += key.Length() + 1;
			}
		}

		for ( const auto& string : context.m_strings )
		{
			auto key = Key( string );
			auto result = uniqueItems.Insert( key );
			if ( result.IsSuccessful() )
			{
				bufferMemory += key.Length() + 1;
			}
		}

		for ( const auto& path : context.m_paths )
		{
			auto key = Key( path );
			auto result = uniqueItems.Insert( key );
			if ( result.IsSuccessful() )
			{
				bufferMemory += key.Length() + 1;
			}
		}
	}
	RED_FATAL_ASSERT( bufferMemory < std::numeric_limits< Uint32 >::max(), "Attempting to map too many strings, need to re-evaluate this code! (%llu strings)", bufferMemory );

	// Pack collected strings into the buffer and add them to the index
	m_lookup.Reserve( uniqueItems.Size() );
	m_index.Reserve( uniqueItems.Size() );
	m_buffer.Resize( static_cast< Uint32 >( bufferMemory ) );

	Uint32 stringsOffset = 0;
	for ( const auto& item : uniqueItems )
	{
		const auto& sv = item.AsStringView();
		const Uint32 length = sv.Length();

		char* bufferPtr = m_buffer.Pointer< char >( stringsOffset );
		red::Memcpy( bufferPtr, sv.Data(), length );
		bufferPtr[ length ] = 0;

		m_lookup.Insert( Key( bufferPtr, length ), stringsOffset );
		m_index.PushBack( { stringsOffset, length } );

		stringsOffset += length + 1;
	}
}

Uint32 StringsMapping::Find( Key&& key ) const
{
	auto iter = m_lookup.Find( key );
	if ( iter != m_lookup.End() )
	{
		return iter.Value();
	}
	return INVALID_INDEX;
}

Uint32 StringsMapping::GetIndex( const CName name ) const
{
	return Find( Key( name ) );
}

Uint32 StringsMapping::GetIndex( const red::StringView& string ) const
{
	return Find( Key( string ) );
}

Uint32 StringsMapping::GetIndex( const res::ResourcePath& path ) const
{
	return Find( Key( path ) );
}

const red::DynArray< fileformat::IndexEntry >& StringsMapping::GetStringsIndex() const
{
	return m_index;
}

red::BlobView StringsMapping::GetStringsBuffer() const
{
	return m_buffer;
}

//------------------------------------------------------------------------------

BinaryWriteContext::BinaryWriteContext( red::BlobSpan bufferSpan )
	: m_bufferSpan( bufferSpan )
	, m_offset( 0 )
{}

template < typename T >
void BinaryWriteContext::Write( const T element )
{
	m_bufferSpan.As< T >( m_offset ) = element;
	m_offset += sizeof( T );
}

void BinaryWriteContext::Write( const Uint8 * ptr, Uint32 size )
{
	RED_FATAL_ASSERT( m_offset + size < m_bufferSpan.Size(), "Overflow when writing %u bytes to buffer", size );
	Uint8* dest = m_bufferSpan.Pointer< Uint8 >( m_offset );
	red::Memcpy( dest, ptr, size );
	m_offset += size;
}

class BinaryWriter : public Writer
{
public:
	BinaryWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context );
	virtual ~BinaryWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;
	virtual WriterArrayScope StartArray( const red::StringView& key, Uint32 elements ) override;

	virtual void WriteName( const red::StringView& key, const CName name ) override;
	virtual void WriteBool( const red::StringView& key, bool value ) override;
	virtual void WriteNumber( const red::StringView& key, Int32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint32 number ) override;
	virtual void WriteNumber( const red::StringView& key, Int64 number ) override;
	virtual void WriteNumber( const red::StringView& key, Uint64 number ) override;
	virtual void WriteFloat( const red::StringView& key, float number ) override;
	virtual void WriteString( const red::StringView& key, const red::StringView& value ) override;

	virtual void WritePath( const red::StringView& key, const res::ResourcePath& value ) override;
	virtual void WriteTimestamp( const red::StringView& key, const Timestamp timestamp ) override;
	virtual void WriteHash( const red::StringView& key, const Hash& hash ) override;

private:
	const StringsMapping& m_stringsMapping;
	BinaryWriteContext& m_context;
};

class BinaryArrayWriter : public WriterArray
{
public:
	BinaryArrayWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context );
	virtual ~BinaryArrayWriter();

	virtual WriterScope StartScope( const red::StringView& key ) override;

	virtual void WriteName( const CName name ) override;
	virtual void WriteNumber( Int32 number ) override;
	virtual void WriteNumber( Uint32 number ) override;
	virtual void WriteNumber( Int64 number ) override;
	virtual void WriteNumber( Uint64 number ) override;
	virtual void WriteFloat( float number ) override;
	virtual void WriteString( const red::StringView& value ) override;
	virtual void WritePath( const res::ResourcePath& value ) override;

private:
	const StringsMapping& m_stringsMapping;
	BinaryWriteContext& m_context;
};

WriterScope CreateBinaryWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context )
{
	return red::CreateUniquePtr< BinaryWriter >( stringsMapping, context );
}

BinaryWriter::BinaryWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context )
	: m_stringsMapping( stringsMapping )
	, m_context( context )
{}

BinaryWriter::~BinaryWriter()
{}

WriterScope BinaryWriter::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< BinaryWriter >( m_stringsMapping, m_context );
}

WriterArrayScope BinaryWriter::StartArray( const red::StringView& key, Uint32 elements )
{
	m_context.Write( elements );
	return red::CreateUniquePtr< BinaryArrayWriter >( m_stringsMapping, m_context );
}

void BinaryWriter::WriteName( const red::StringView& key, const CName name )
{
	Uint32 index = m_stringsMapping.GetIndex( name );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}

void BinaryWriter::WriteBool( const red::StringView& key, bool value )
{
	Uint8 val = value ? 1 : 0;
	m_context.Write( val );
}

void BinaryWriter::WriteNumber( const red::StringView& key, Int32 number )
{
	m_context.Write( number );
}

void BinaryWriter::WriteNumber( const red::StringView& key, Uint32 number )
{
	m_context.Write( number );
}

void BinaryWriter::WriteNumber( const red::StringView& key, Int64 number )
{
	m_context.Write( number );
}

void BinaryWriter::WriteNumber( const red::StringView& key, Uint64 number )
{
	m_context.Write( number );
}

void BinaryWriter::WriteFloat( const red::StringView& key, float number )
{
	m_context.Write( number );
}

void BinaryWriter::WriteString( const red::StringView& key, const red::StringView& value )
{
	Uint32 index = m_stringsMapping.GetIndex( value );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}

void BinaryWriter::WritePath( const red::StringView& key, const res::ResourcePath& value )
{
	Uint32 index = m_stringsMapping.GetIndex( value );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}

void BinaryWriter::WriteTimestamp( const red::StringView& key, const Timestamp timestamp )
{
	m_context.Write( timestamp );
}

void BinaryWriter::WriteHash( const red::StringView& key, const Hash& hash )
{
	const archive::FileHash& fileHash = Hash::ToFileHash( hash );
	m_context.Write( fileHash.value, archive::FileHash::NUM_BYTES );
}


BinaryArrayWriter::BinaryArrayWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context )
	: m_stringsMapping( stringsMapping )
	, m_context( context )
{
}

BinaryArrayWriter::~BinaryArrayWriter()
{
}

WriterScope BinaryArrayWriter::StartScope( const red::StringView& key )
{
	return red::CreateUniquePtr< BinaryWriter >( m_stringsMapping, m_context );
}

void BinaryArrayWriter::WriteName( const CName name )
{
	Uint32 index = m_stringsMapping.GetIndex( name );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}

void BinaryArrayWriter::WriteNumber( Int32 number )
{
	m_context.Write( number );
}

void BinaryArrayWriter::WriteNumber( Uint32 number )
{
	m_context.Write( number );
}

void BinaryArrayWriter::WriteNumber( Int64 number )
{
	m_context.Write( number );
}

void BinaryArrayWriter::WriteNumber( Uint64 number )
{
	m_context.Write( number );
}

void BinaryArrayWriter::WriteFloat( float number )
{
	m_context.Write( number );
}

void BinaryArrayWriter::WriteString( const red::StringView& value )
{
	Uint32 index = m_stringsMapping.GetIndex( value );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}

void BinaryArrayWriter::WritePath( const res::ResourcePath& value )
{
	Uint32 index = m_stringsMapping.GetIndex( value );
	RED_FATAL_ASSERT( index != StringsMapping::INVALID_INDEX );
	m_context.Write( index );
}


} // prv
} // dependency
