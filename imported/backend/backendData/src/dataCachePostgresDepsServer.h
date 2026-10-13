/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataCacheServer.h"

namespace data
{
namespace prv
{

class PostgresBatchWorker;

class PostgresDepsServer : public DepsServer
{
	RTTI_DECLARE_TYPE( PostgresDepsServer );
public:
	PostgresDepsServer();
	~PostgresDepsServer();

	virtual bool Initialise( const red::String& params, job::Counter& counter ) override;
	virtual void Shutdown() override;

	virtual bool IsInitialised() const override;

	virtual Cache::Result GetExactOutputList( const dependency::Hash& prereqHash, dependency::OutputFileList& outputList, job::Builder& builder ) override;
	virtual Cache::Result GetLatestOutputList( const res::ResourcePath& path, dependency::OutputFileList& outputList, job::Builder& builder ) override;

private:
	red::UniquePtr< PostgresBatchWorker > m_worker;
	red::Atomic< bool > m_shutdownFlag;
};

} // prv
} // data
