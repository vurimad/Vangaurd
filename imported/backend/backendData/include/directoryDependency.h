/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyTypes.h"

// Create a dependency on a pattern of files within a directory or directory tree
// This handles the situation when files are added or removed from a directory gracefully
BACKEND_DATA_API dependency::DependencyUniquePtr MakeDirectoryDependency( const red::StringView& directory, const red::StringView& pattern, bool recursive = false, data::DepotLocation depot = data::DepotLocation::Source, dependency::Requirement require = dependency::Requirement::Normal );

namespace dependency
{

// Dependency on a directory of resources from the source depot only
// This will automatically include or exclude files as they are added or removed from the directory
class BACKEND_DATA_API DirectoryDependency : public Dependency
{
	RTTI_DECLARE_TYPE( DirectoryDependency );
public:
	DirectoryDependency();
	DirectoryDependency( const red::StringView& directory, const red::StringView& pattern, bool recursive, data::DepotLocation location, Requirement requireLevel );
	virtual ~DirectoryDependency();

	DirectoryDependency( const DirectoryDependency& );
	DirectoryDependency( DirectoryDependency&& );
	DirectoryDependency& operator= ( const DirectoryDependency& );
	DirectoryDependency& operator= ( DirectoryDependency&& );

	virtual DependencyUniquePtr Clone() const override;

	virtual bool Load( Reader& reader ) override;
	virtual bool Save( Writer& writer ) const override;

	virtual void Update( data::SystemContext& context ) override;

	virtual bool DumpDebugInfo( Writer& writer ) const override;

	virtual red::StringView GetPathString() const override;
	virtual Uint64 GetPathHash() const override;

	red::StringView GetDirectory() const;
	red::StringView GetPattern() const;
	bool IsRecursive() const;

private:
	void ComputePathValues();

	red::String m_directory;
	red::String m_pattern;
	red::String m_combinedPath;
	Uint64 m_combinedHash;
	bool m_recursive;

	FileInfoList m_scannedResources;
};

//-----------------------------------------------------------------------------

RED_INLINE red::StringView DirectoryDependency::GetDirectory() const
{
	return m_directory;
}

RED_INLINE red::StringView DirectoryDependency::GetPattern() const
{
	return m_pattern;
}

RED_INLINE bool DirectoryDependency::IsRecursive() const
{
	return m_recursive;
}

} // dependency
