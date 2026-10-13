/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace data
{

class BACKEND_DATA_API WorkerTaskAccumulator
{
	RED_USE_MEMORY_POOL( red::PoolBackend );
public:
	WorkerTaskAccumulator();
	~WorkerTaskAccumulator();

	void AddTask( const res::ResourcePath& path );

	bool SaveList( const red::AbsolutePath& outputPath );

private:
	red::DynArray< res::ResourcePath > m_taskPaths;
	red::SpinLock m_guard;
};

} // data
