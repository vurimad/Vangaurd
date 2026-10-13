/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyReader.h"
#include "dependencyBinaryFileFormat.hpp"

namespace dependency
{
namespace prv
{

// Class to unmap indexes and turn them into names/strings/resource paths
class StringsUnmapper
{
public:
	static constexpr Uint32 INVALID_INDEX = std::numeric_limits< Uint32 >::max();

	StringsUnmapper( const red::ArraySpan< const fileformat::IndexEntry >& indexEntries, red::BlobView& stringsData );

	CName GetName( Uint32 index ) const;
	red::String GetString( Uint32 index ) const;
	res::ResourcePath GetResourcePath( Uint32 index ) const;

private:
	red::StringView Find( Uint32 index ) const;

	const red::ArraySpan< const fileformat::IndexEntry > m_index;
	const red::BlobView m_strings;
};


// Simple wrapper class to simplify serialising data from a buffer
class BinaryReadContext
{
public:
	explicit BinaryReadContext( red::BlobView bufferView );

	template < typename T >
	void Read( T& element );

	void Read( Uint8* ptr, Uint32 size );

private:
	red::BlobView m_bufferView;
	Uint32 m_offset;
};


ReaderScope CreateBinaryReader( const StringsUnmapper& stringsUnmapper, BinaryReadContext& context );

} // prv
} // dependency
