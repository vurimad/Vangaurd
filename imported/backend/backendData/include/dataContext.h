/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dataTypes.h"

namespace dependency
{
class OutputCollector;
}

namespace data
{

// Context provded to the generator and cooker classes
// Provides a way to interact with the various data systems
// including resource depots and loaders.
class BACKEND_DATA_API Context
{
public:
	data::ResourceDepot& GetResourceDepot( data::DepotLocation location ) const;
	data::ResourceLoader& GetResourceLoader( data::DepotLocation location ) const;

	data::ResourceDepot& SourceDepot() const;
	data::ResourceDepot& CookedDepot() const;
	data::ResourceLoader& SourceLoader() const;
	data::ResourceLoader& CookedLoader() const;

	resdb::Database& SourceResourceDatabase() const;

	dependency::OutputCollector* GetOutputCollector() const;

	data::ErrorHandler& ErrorHandler() const;

	data::WorkerTaskAccumulator* WorkerTaskAccumulator() const;

	data::TargetPlatform TargetPlatform() const;

	bool IsServerBuild() const;
	bool IsServerWorker() const;

protected:
	Context();
	~Context();

	data::ResourceDepot* m_sourceDepot;
	data::ResourceDepot* m_cookedDepot;

	data::ResourceLoader* m_sourceLoader;
	data::ResourceLoader* m_cookedLoader;

	resdb::Database* m_sourceDepotDatabase;

	dependency::OutputCollector * m_outputCollector;

	data::ErrorHandler* m_errorHandler;

	data::WorkerTaskAccumulator* m_workerTaskAccumulator;

	data::TargetPlatform m_targetPlatform;

	bool m_isServer;
	bool m_isWorker;
};

//------------------------------------------------------------------------------

RED_INLINE data::ResourceDepot& Context::SourceDepot() const
{
	return *m_sourceDepot;
}

RED_INLINE data::ResourceDepot& Context::CookedDepot() const
{
	return *m_cookedDepot;
}

RED_INLINE data::ResourceLoader& Context::SourceLoader() const
{
	return *m_sourceLoader;
}

RED_INLINE data::ResourceLoader& Context::CookedLoader() const
{
	return *m_cookedLoader;
}

RED_INLINE resdb::Database& Context::SourceResourceDatabase() const
{
	return *m_sourceDepotDatabase;
}

RED_INLINE data::ErrorHandler& Context::ErrorHandler() const
{
	return *m_errorHandler;
}

RED_INLINE data::WorkerTaskAccumulator* Context::WorkerTaskAccumulator() const
{
	return m_workerTaskAccumulator;
}

RED_INLINE data::TargetPlatform Context::TargetPlatform() const
{
	return m_targetPlatform;
}

RED_INLINE bool Context::IsServerBuild() const
{
	return m_isServer;
}

RED_INLINE bool Context::IsServerWorker() const
{
	return m_isWorker;
}

RED_INLINE dependency::OutputCollector* Context::GetOutputCollector() const
{
	return m_outputCollector;
}

} // data
