/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyDatabase.h"

#include "../../../common/redMemory/include/linearAllocator.h"

namespace dependency
{

class BinaryDatabase : public Database
{
public:
	explicit BinaryDatabase( const red::AbsolutePath& path, Uint32 settingsHash );
	~BinaryDatabase();

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
	const ResourceInfo* FindEntry( const res::ResourcePath& resourcePath ) const;

	red::memory::DynamicLinearAllocator m_entryStorage;

	using EntryMap = red::HashMap< res::ResourcePath, ResourceInfo* >;
	EntryMap m_map;

	using ReadLock = red::ScopedSharedLock< red::RWSpinLock >;
	using WriteLock = red::ScopedLock< red::RWSpinLock >;
	mutable red::RWSpinLock m_guard;

	const red::AbsolutePath m_path;
	const Uint32 m_settingsHash;

	bool m_changed;

};

} // dependency
