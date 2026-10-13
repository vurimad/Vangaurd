/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyDatabase.h"

#include "../../../common/redMemory/include/linearAllocator.h"

namespace dependency
{

// Loose file dependency database
// The dependencies are stored as loose json files named after the resource path hash identifier.
// For file system efficiency they are separated into a two level deep hierarchy.
class LooseFileDatabase : public Database
{
public:
	LooseFileDatabase( const red::AbsolutePath& path, Uint32 settingsHash );
	~LooseFileDatabase();

	virtual bool Load() override;
	virtual bool Save() override;
	virtual void Clear() override;
	virtual bool HasChanged() override;

	virtual void SetResourceInfo( const ResourceInfo& info ) override;
	virtual void ClearResourceInfo( const res::ResourcePath& resourcePath ) override;
	virtual const ResourceInfo* GetResourceInfo( const res::ResourcePath& resourcePath ) override;
	virtual const ResourceInfo* GetResourceInfo( const res::ResourcePath& resourcePath ) const override;

	virtual red::DynArray< const ResourceInfo* > GetAllResourceInfos() const override;

	static const char* DefaultDatabaseName();

private:
	using Key = res::ResourcePath;
	struct Entry
	{
		dependency::ResourceInfo info;
		Uint64 timestamp;
	};

	bool SaveEntry( const Key& key, Entry* entry );

	class Metrics
	{
	public:
		Metrics();

		void Reset();

		void FileLoaded( double deltaMs );
		void FileSaved( double deltaMs );

		void Report();

	private:
		red::RWSpinLock m_guard;

		Uint32 m_filesLoaded;
		Uint32 m_filesSaved;
		double m_loadingTimeMs;
		double m_savingTimeMs;
	};

	// TODO: Make some of these helper functions public for testing purposes
	red::AbsolutePath ComputePath( const res::ResourcePath& resourcePath ) const;
	bool WriteFile( const red::AbsolutePath& path, const ResourceInfo& info ) const;
	bool ReadFile( const red::AbsolutePath& path, ResourceInfo& info ) const;

	red::memory::DynamicLinearAllocator m_entryStorage;

	using EntryMap = red::HashMap< Key, Entry* >;
	EntryMap m_map;

	using ReadLock = red::ScopedSharedLock< red::RWSpinLock >;
	using WriteLock = red::ScopedLock< red::RWSpinLock >;
	mutable red::RWSpinLock m_guard;

	const red::AbsolutePath m_rootPath;
	const Uint32 m_settingsHash;

	bool m_changed;

	mutable Metrics m_metrics;
};

} // dependency
