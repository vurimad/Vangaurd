/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataCacheServer.h"

namespace res
{
namespace prv
{
class SingleTreeResourceDepot;
}
}

namespace dependency
{
class Database;
class Hash;
}

namespace data
{
namespace prv
{

// Simple almost no-op server cache implementation which only works with a single generated data set
// This is intended to be used when testing or benchmarking the rest of the data system so no network
// or other machine lag gets measured

// Simple file server cache which just loads a single tree depot to access the files
// NOTE: This only supports a single resource depot tree and therefore a single version of the resources
class DebugFileServer : public FileServer
{
	RTTI_DECLARE_TYPE( DebugFileServer );
public:
	DebugFileServer();
	~DebugFileServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder ) override;

private:
	red::AbsolutePath m_rootPath;
	red::UniquePtr< res::prv::SingleTreeResourceDepot > m_depot;
};

// Simple dependency server cache which just loads dependencies from disk, either in json or in a binary format.
class DebugDepsServer : public DepsServer
{
	RTTI_DECLARE_TYPE( DebugDepsServer );
public:
	DebugDepsServer();
	~DebugDepsServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder ) override;
	virtual Cache::Result GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder ) override;

private:
	red::AbsolutePath m_depsPath;
	red::UniquePtr< dependency::Database > m_database;
	red::HashMap< dependency::Hash, const dependency::ResourceInfo* > m_prereqMap;
};

} // prv
} // data
