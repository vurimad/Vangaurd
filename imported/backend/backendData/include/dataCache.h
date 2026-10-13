/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyTypesImpl.h"
#include "dataResult.h"

#include "../../../common/redMemory/include/function.h"

namespace data
{

namespace prv
{
class DepsServer;
using DepsServerPtr = red::UniquePtr< DepsServer >;

class FileServer;
using FileServerPtr = red::UniquePtr< FileServer >;
}

class Result;

// Interface for using a cooked data cache during data building
class BACKEND_DATA_API Cache
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
public:
	using Result = data::Result;

	using ConnectCallback = void ( red::UniquePtr< Cache >&& );
	static void Connect( const red::String& configName, job::Counter& waitCounter, red::Function< ConnectCallback > onConnect );

	Cache( prv::DepsServerPtr&& depsServer, prv::FileServerPtr&& fileServer );
	~Cache();

	void Shutdown();

	bool IsInitialised() const;

	Result GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder );
	Result GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder );
	Result GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder );

private:
	prv::DepsServerPtr m_depsServer;
	prv::FileServerPtr m_fileServer;
};

namespace prv
{

struct CacheConfig
{
	CName depsServerType;
	String depsServerParams;
	CName fileServerType;
	String fileServerParams;
};

BACKEND_DATA_API bool FindCacheConfig( const red::String& configName, CacheConfig& result );
} // prv

} // data
