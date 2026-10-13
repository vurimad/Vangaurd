/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/resourcePath.h"
#include "../../../common/redContainers/include/hashMap.h"
#include "../../../common/redContainers/include/set.h"

#include "../../backendData/include/dependencyScannerImpl.h"

namespace res
{
class ResourceEditorDepot;
} // res


namespace commandlet
{

// How to order the dependencies stored internally
enum class DependencyOrder
{
	DONT_CARE = 0,
	BY_HASH,
	BY_PATH,
};

class DependencyEntry
{
public:
	using Status = dependency::prv::ScanResult;

	DependencyEntry();

	RED_INLINE const res::ResourcePath& Path() const { return m_path; }
	RED_INLINE Uint32 Level() const { return m_level; }
	RED_INLINE Status GetStatus() const { return m_status; }
	RED_INLINE bool IsOK() const { return m_status == Status::OK; }
	RED_INLINE bool IsLink() const { return m_isLink; }

	RED_INLINE red::ArraySpan<const res::ResourcePath> Dependencies() const { return m_dependencies; }

	const char* GetStatusMessage() const;

	static DependencyEntry ScanResource( const res::ResourcePath& path, res::ResourceEditorDepot* editorDepot, DependencyOrder order );

private:
	explicit DependencyEntry( const res::ResourcePath& resourcePath );

	res::ResourcePath m_path;
	Uint32 m_level;
	Status m_status;
	bool m_isLink;

	red::DynArray<res::ResourcePath> m_dependencies;
};

bool operator==( const DependencyEntry& left, const DependencyEntry& right );
bool operator!=( const DependencyEntry& left, const DependencyEntry& right );
bool operator<( const DependencyEntry& left, const DependencyEntry& right );


class DependencyMap
{
public:
	explicit DependencyMap( res::ResourceEditorDepot* editorDepot, DependencyOrder order = DependencyOrder::DONT_CARE );

	// Scans a resource (and its dependencies) for dependencies and internally creates 
	// a DependencyEntry for every resource it encounters.
	void ScanResourceRecursive( const res::ResourcePath& path );

	// Prepare internal data structures to handle recursive dependency lookups
	void PrepareRecursiveDependencies();

	const red::Set<res::ResourcePath>& CollectRecursive( const res::ResourcePath& path );

	const DependencyEntry* Get( const res::ResourcePath& path ) const;
	red::DynArray<const DependencyEntry*> Get( red::ArraySpan<const res::ResourcePath> paths ) const;

	red::DynArray<res::ResourcePath> DetectBadLevelReferences( const res::ResourcePath& path ) const;
	red::DynArray<res::ResourcePath> DetectReverseReferences( const res::ResourcePath& path ) const;

private:
	red::HashMap< const res::ResourcePath, DependencyEntry > m_entryMap;
	red::HashMap< const res::ResourcePath, red::Set<res::ResourcePath> > m_recursiveDepsCache;
	red::HashSet< res::ResourcePath > m_recursiveVisitedDeps;

	res::ResourceEditorDepot* m_editorDepot;
	DependencyOrder m_ordering;
};

} // commandlet
