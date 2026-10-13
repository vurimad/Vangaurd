/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dataCacheServer.h"

namespace data
{
namespace prv
{


class HttpFileServer : public FileServer
{
	RTTI_DECLARE_TYPE( HttpFileServer );
public:
	HttpFileServer();
	~HttpFileServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetResource( const res::ResourcePath& path, const dependency::Hash& hash, data::ResourceDepot& depot, job::Builder& builder ) override;

private:
	red::String ComputeRemoteUrl( const res::ResourcePath& path, const dependency::Hash& hash ) const;

	red::String m_rootUrl;
};

} // prv
} // data
