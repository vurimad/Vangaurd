/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataCacheServer.h"

namespace data
{
namespace prv
{

// Simple implementation of a data cache file server just mapped to a single location on some disk somewhere
// Uses absolute path for the root path so it is limited to drives and paths
// The structure on disk must be in: (where all components are hex)
// <First 2 Hex Digits of RPH>/<Second 2 Hex Digits of RPH>/<ResourcePath Hash>_<Content Hash>
// NOTE: This should only be used for testing and development
class WindowsFileServer : public FileServer
{
	RTTI_DECLARE_TYPE( WindowsFileServer );
public:
	WindowsFileServer();
	~WindowsFileServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder ) override;

private:
	red::AbsolutePath ComputeEncodedPath( const res::ResourcePath& path, const dependency::Hash& hash ) const;

	red::AbsolutePath m_rootPath;
};

} // prv
} // data
