/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataPlatform.h"
#include "dependencyTypes.h"
#include "dependencyTypesImpl.h"

namespace dependency
{

// Interface for managing the stored dependency information on disk
class BACKEND_DATA_API Database
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	// Create the dependency database system based on the input path
	// If path is a file then it must point to a dependency database binary file
	// If path is a directory then it must point to an existing directory where the
	// dependencies are stored as loose files with a hierarchical hex based naming convention
	// The settingsHash is a key used to determine when the fundamental settings of the
	// generator system have changed and the dependency database is no longer valid
	static red::UniquePtr< Database > Create( const red::AbsolutePath& path, Uint32 settingsHash );

	// Returns the default database name to be appended to a directory path
	static red::AbsolutePath ComputeDefaultDatabasePath( const red::AbsolutePath& basePath );

	virtual ~Database() = 0;

	// Loads the database from disk
	virtual bool Load() = 0;

	// Saves the database to disk
	virtual bool Save() = 0;

	// Clears the internal data of the database, does not affect any files on disk
	virtual void Clear() = 0;

	// Returns if the database has changed since it was last loaded or saved
	virtual bool HasChanged() = 0;

	// Sets the resource info for a particular resource in the database, creating
	// a new entry or overriding an existing one.
	virtual void SetResourceInfo( const ResourceInfo& info ) = 0;

	// Removes the resource info for a particular resource from the database.
	virtual void ClearResourceInfo( const res::ResourcePath& resourcePath ) = 0;

	// Gets the resource info for a particular resource from the database. 
	virtual const ResourceInfo* GetResourceInfo( const res::ResourcePath& resourcePath ) = 0;

	// Gets the resource info for a particular resource from the database based only on information that is loaded into memory
	virtual const ResourceInfo* GetResourceInfo( const res::ResourcePath& resourcePath ) const = 0;

	// Get all of the resource infos stored in this database
	virtual red::DynArray< const ResourceInfo* > GetAllResourceInfos() const = 0;
};

} // dependency
