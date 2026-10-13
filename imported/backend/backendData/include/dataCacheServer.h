/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataCache.h"

namespace data
{
namespace prv
{

class DepsServer;
using DepsServerPtr = red::UniquePtr< DepsServer >;

class FileServer;
using FileServerPtr = red::UniquePtr< FileServer >;

// Interface for the dependency retrieval component of the data cache
class BACKEND_DATA_API DepsServer
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( DepsServer );
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
public:
	virtual ~DepsServer() = 0;

	virtual bool Initialise( const red::String& params, job::Counter& counter ) = 0;
	virtual void Shutdown() = 0;

	virtual bool IsInitialised() const = 0;

	virtual Cache::Result GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder ) = 0;
	virtual Cache::Result GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder ) = 0;

	static DepsServerPtr Create( CName name );
};


// Interface for the file/resource retrieval component of the data cache
class BACKEND_DATA_API FileServer
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( FileServer );
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
public:
	virtual ~FileServer() = 0;

	virtual bool Initialise( const red::String& params, job::Counter& counter ) = 0;
	virtual void Shutdown() = 0;

	virtual bool IsInitialised() const = 0;

	virtual Cache::Result GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder ) = 0;

	static FileServerPtr Create( CName name );
};

} // prv
} // data
