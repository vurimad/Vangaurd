/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTypes.h"
#include "dependencyHash.h"

#include "../../../common/redContainers/include/dynArray.h"

namespace data
{
class SystemContext;
}

namespace dependency
{
//------------------------------------------------------------------------------

enum class Requirement : Uint8
{
	Normal,		// Should exist but build will continue with error
	Required,	// Must exist otherwise build will fail
	Optional,	// May exist but build will continue with warning
};

using Timestamp = Uint64;

//------------------------------------------------------------------------------

struct FileInfo
{
	res::ResourcePath path;
	Timestamp timestamp;
	Hash hash;
};

BACKEND_DATA_API bool operator==( const FileInfo& left, const FileInfo& right );
BACKEND_DATA_API bool operator!=( const FileInfo& left, const FileInfo& right );
BACKEND_DATA_API bool operator<( const FileInfo& left, const FileInfo& right );

// Helper functions to enable BasicInfo classes to be stored in sorted arrays and sets
BACKEND_DATA_API bool operator==( const FileInfo& left, const res::ResourcePath& right );
BACKEND_DATA_API bool operator==( const res::ResourcePath& left, const FileInfo& right );
BACKEND_DATA_API bool operator!=( const FileInfo& left, const res::ResourcePath& right );
BACKEND_DATA_API bool operator!=( const res::ResourcePath& left, const FileInfo& right );
BACKEND_DATA_API bool operator<( const FileInfo& left, const res::ResourcePath& right );
BACKEND_DATA_API bool operator<( const res::ResourcePath& left, const FileInfo& right );

using FileInfoList = red::SortedArray< FileInfo >;

//------------------------------------------------------------------------------

class Dependency;
using DependencyPtr = red::SharedPtr< Dependency >;
using DependencyWeakPtr = red::WeakPtr< Dependency >;
using DependencyUniquePtr = red::UniquePtr< Dependency >;
using DependencyList = red::DynArray< DependencyPtr >;

class Writer;
class Reader;

class BACKEND_DATA_API Dependency
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
	RTTI_DECLARE_POLYMORPHIC_TYPE( Dependency );

public:
	Dependency();
	Dependency( data::DepotLocation depot, Requirement require );
	virtual ~Dependency();

	virtual DependencyUniquePtr Clone() const = 0;

	virtual bool Load( Reader& reader );
	virtual bool Save( Writer& writer ) const;

	virtual void Update( data::SystemContext& context ) = 0;

	virtual bool DumpDebugInfo( Writer& writer ) const = 0;

	virtual red::StringView GetPathString() const = 0;
	virtual Uint64 GetPathHash() const = 0;

	virtual res::ResourcePath GetResourcePath() const;

	Timestamp GetTimeStamp() const;
	const Hash& GetHash() const;
	data::DepotLocation GetDepotLocation() const;
	Requirement GetRequirementLevel() const;
	bool IsGenerated() const;
	bool IsChild() const;
	bool RescanOnChanged() const;
	bool IsValid() const;

protected:
	// Protected to prevent slicing of objects
	Dependency( const Dependency& );
	Dependency( Dependency&& );
	Dependency& operator= ( const Dependency& );
	Dependency& operator= ( Dependency&& );

	void SetTimestampHash( Timestamp timestamp, Hash hash );
	void SetValid( bool valid );

	Timestamp m_timestamp;
	Hash m_hash;

	data::DepotLocation m_depot;
	Requirement m_require;
	bool m_generated;
	bool m_child;
	bool m_rescan;
	bool m_valid;
};

bool operator<( const DependencyPtr& left, const DependencyPtr& right );

//------------------------------------------------------------------------------

RED_INLINE Timestamp Dependency::GetTimeStamp() const
{
	return m_timestamp;
}

RED_INLINE const Hash& Dependency::GetHash() const
{
	return m_hash;
}

RED_INLINE data::DepotLocation Dependency::GetDepotLocation() const
{
	return m_depot;
}

RED_INLINE Requirement Dependency::GetRequirementLevel() const
{
	return m_require;
}

RED_INLINE bool Dependency::IsGenerated() const
{
	return m_generated;
}

RED_INLINE bool Dependency::IsChild() const
{
	return m_child;
}

RED_INLINE bool Dependency::RescanOnChanged() const
{
	return m_rescan;
}

RED_INLINE bool Dependency::IsValid() const
{
	return m_valid;
}

RED_INLINE bool operator<( const DependencyPtr& left, const DependencyPtr& right )
{
	return left->GetPathHash() < right->GetPathHash();
}

//------------------------------------------------------------------------------

RED_INLINE bool operator==( const FileInfo& left, const FileInfo& right )
{
	return left.path == right.path &&
		left.timestamp == right.timestamp &&
		left.hash == right.hash;
}

RED_INLINE bool operator!=( const FileInfo& left, const FileInfo& right )
{
	return !( left == right );
}

RED_INLINE bool operator<( const FileInfo& left, const FileInfo& right )
{
	return left.path < right.path;
}

RED_INLINE bool operator==( const FileInfo& left, const res::ResourcePath& right )
{
	return left.path == right;
}

RED_INLINE bool operator==( const res::ResourcePath& left, const FileInfo& right )
{
	return left == right.path;
}

RED_INLINE bool operator!=( const FileInfo& left, const res::ResourcePath& right )
{
	return left.path != right;
}

RED_INLINE bool operator!=( const res::ResourcePath& left, const FileInfo& right )
{
	return left != right.path;
}

RED_INLINE bool operator<( const FileInfo& left, const res::ResourcePath& right )
{
	return left.path < right;
}

RED_INLINE bool operator<( const res::ResourcePath& left, const FileInfo& right )
{
	return left < right.path;
}

//-----------------------------------------------------------------------------

} // dependency
