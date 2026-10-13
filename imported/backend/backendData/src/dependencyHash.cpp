/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyHash.h"

#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redMemory/include/uniqueBuffer.h"
#include "../../../common/redSystem/include/hex.h"

#include "../../../common/archive/include/common.h"

static_assert( ::crypto::Ssha1::NUM_BYTES == archive::FileHash::NUM_BYTES, "Different hash stored in archive and dependency system!" );
static_assert( sizeof(dependency::Hash) == sizeof(archive::FileHash), "Size of archive::FileHash and dependency::Hash must match!" );

namespace dependency
{

Hash Hash::Compute( const red::BlobView buffer )
{
	Hash result;
	::crypto::CalculateSha1_Insecure( buffer.Data(), buffer.Size(), result.m_value );
	return result;
}

Hash Hash::Compute( const red::UniqueBuffer& buffer )
{
	Hash result;
	::crypto::CalculateSha1_Insecure( buffer.Get(), buffer.GetSize(), result.m_value );
	return result;
}

Hash::Hash()
{
	Clear();
}

Hash::Hash( const ::crypto::Ssha1& value )
{
	red::Memcpy( m_value.m_value, value.m_value, ::crypto::Ssha1::NUM_BYTES );
}

void Hash::Clear()
{
	red::Memzero( m_value.m_value, ::crypto::Ssha1::NUM_BYTES );
}

bool Hash::IsEmpty() const
{
	for ( Uint32 i = 0; i < ::crypto::Ssha1::NUM_BYTES; ++i )
	{
		if ( m_value.m_value[i] != 0 )
		{
			return false;
		}
	}
	return true;
}

constexpr Uint32 EncodedHashLength = red::HexEncodedSize( ::crypto::Ssha1::NUM_BYTES );

red::String Hash::ToString() const
{
	red::String result;
	result.Resize( EncodedHashLength );
	bool success = red::HexEncode( m_value.m_value, ::crypto::Ssha1::NUM_BYTES, result.AsChar(), result.Capacity() );
	RED_FATAL_ASSERT( success, "Error in printing dependency hash to string" );
	return result;
}

red::BlobView Hash::ToBlobView() const
{
	return red::BlobView( m_value.m_value, ::crypto::Ssha1::NUM_BYTES );
}

Hash Hash::FromEncodedString( const red::StringView str )
{
	Hash result;
	if ( str.Length() >= EncodedHashLength )
	{
		constexpr Uint32 bufferSize = red::HexDecodedSize( EncodedHashLength );
		Uint8 buffer[ bufferSize ];
		if ( red::HexDecode( str.Data(), str.Length(), buffer, bufferSize ) )
		{
			red::Memcpy( result.m_value.m_value, buffer, ::crypto::Ssha1::NUM_BYTES );
		}
		else
		{
			RED_FATAL( "Error in decoding dependency hash string" );
		}
	}
	return result;
}

String Hash::ToEncodedString( const Hash& hash )
{
	String result;
	result.Resize( EncodedHashLength );
	bool success = red::HexEncode( hash.m_value.m_value, ::crypto::Ssha1::NUM_BYTES, result.AsChar(), EncodedHashLength + 1 );
	RED_FATAL_ASSERT( success, "Error in encoding dependency hash string" );
	return result;
}

const Hash& Hash::FromFileHash( const archive::FileHash& fileHash )
{
	return reinterpret_cast<const Hash&>( fileHash );
}

const archive::FileHash& Hash::ToFileHash( const Hash& hash )
{
	return reinterpret_cast<const archive::FileHash&>( hash.m_value );
}

bool operator==( const Hash& left, const Hash& right )
{
	return red::Memcmp( left.m_value.m_value, right.m_value.m_value, ::crypto::Ssha1::NUM_BYTES ) == 0;
}

bool operator!=( const Hash& left, const Hash& right )
{
	return !(left == right);
}

bool operator<( const Hash& left, const Hash& right )
{
	return red::Memcmp( left.m_value.m_value, right.m_value.m_value, ::crypto::Ssha1::NUM_BYTES ) < 0;
}

} // dependency