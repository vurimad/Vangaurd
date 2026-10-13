/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redCrypto/include/sha1.h"

namespace red
{
class BlobView;
class UniqueBuffer;
}

namespace archive
{
struct FileHash;
}

namespace dependency
{

// Hash class that wraps the chosen hash algorithm used in dependency tracking
// Currently uses SHA1 which is a reasonable choice between size of hash data and
// speed of hashing.
class BACKEND_DATA_API Hash
{
public:
	// Create a new hash from the given data in the buffer
	static Hash Compute( const red::BlobView buffer );
	static Hash Compute( const red::UniqueBuffer& buffer );

	// Creates an empty or null hash
	Hash();

	explicit Hash( const ::crypto::Ssha1& value );

	// Clears the hash back to an empty or null state
	void Clear();

	bool IsEmpty() const;

	// Returns a "user friendly" printable string of the hash
	red::String ToString() const;

	red::BlobView ToBlobView() const;

	friend BACKEND_DATA_API bool operator==( const Hash& left, const Hash& right );
	friend BACKEND_DATA_API bool operator!=( const Hash& left, const Hash& right );
	friend BACKEND_DATA_API bool operator<( const Hash& left, const Hash& right );

	// Helper static functions for text based serialization
	static Hash FromEncodedString( const red::StringView str );
	static String ToEncodedString( const Hash& hash );

	// Helper casting functions
	static const Hash& FromFileHash( const archive::FileHash& fileHash );
	static const archive::FileHash& ToFileHash( const Hash& hash );

private:
	// TODO Replace this with different SHA1 computation code as it is slow,
	// and writes over the data it is computing, meaning that it cannot be used
	// with memory mapped IO
	::crypto::Ssha1 m_value;
};

} // dependency

namespace red
{
template <>
RED_INLINE red::THash32 GetHash< dependency::Hash >( const dependency::Hash& hash )
{
	return hash.ToBlobView().As< red::THash32 >();
}

}
