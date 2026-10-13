/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyWriter.h"
#include "dependencyBinaryFileFormat.hpp"

namespace dependency
{
namespace prv
{

//------------------------------------------------------------------------------

// Context to accumulate information to about each resource info object that we want to serialise
// Collects all string related data for de-duplication and accumulates how much memory will be needed
struct MappingContext
{
	red::DynArray< CName > m_names;
	red::DynArray< res::ResourcePath > m_paths;
	red::DynArray< String > m_strings;

	Uint32 m_memoryRequired = 0;
};

WriterScope CreateMappingWriter( MappingContext& context );

//------------------------------------------------------------------------------

// Object to hold the strings buffer and to maintain a mapping between name/string/path to offset within the buffer
class StringsMapping
{
public:
	static constexpr Uint32 INVALID_INDEX = std::numeric_limits< Uint32 >::max();

	void AddCollectedStrings( const red::DynArray< MappingContext >& collectedStrings );

	Uint32 GetIndex( const CName name ) const;
	Uint32 GetIndex( const red::StringView& string ) const;
	Uint32 GetIndex( const res::ResourcePath& path ) const;

	const red::DynArray< fileformat::IndexEntry >& GetStringsIndex() const;
	red::BlobView GetStringsBuffer() const;

private:
	class Key
	{
	public:
		Key() = default;
		explicit Key( const CName name );
		explicit Key( const red::StringView& string );
		explicit Key( const res::ResourcePath& path );
		Key( const char* ptr, Uint32 length );

		Key( const Key& ) = default;
		Key& operator=( const Key& ) = default;

		bool operator==( const Key& other ) const;
		bool operator<( const Key& other ) const;
		Uint32 CalcHash() const;

		Uint32 Length() const;
		const red::StringView& AsStringView() const;

	private:
		red::StringView m_string;
	};

	Uint32 Find( Key&& key ) const;

	red::HashMap< Key, Uint32 > m_lookup;
	red::Blob m_buffer;
	red::DynArray< fileformat::IndexEntry > m_index;
};

// Simple wrapper class to simplify serialising data to a buffer
class BinaryWriteContext
{
public:
	explicit BinaryWriteContext( red::BlobSpan bufferSpan );

	template < typename T >
	void Write( const T element );

	void Write( const Uint8* ptr, Uint32 size );

private:
	red::BlobSpan m_bufferSpan;
	Uint32 m_offset;
};

WriterScope CreateBinaryWriter( const StringsMapping& stringsMapping, BinaryWriteContext& context );

//------------------------------------------------------------------------------

} // prv
} // dependency
