/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataCacheServer.h"

namespace data
{
namespace prv
{

class WebServiceDepsServer : public DepsServer
{
	RTTI_DECLARE_TYPE( WebServiceDepsServer );
public:
	WebServiceDepsServer();
	~WebServiceDepsServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder ) override;
	virtual Cache::Result GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder ) override;

private:
	red::String m_requestRootUrl;
};

} // prv
} // data
