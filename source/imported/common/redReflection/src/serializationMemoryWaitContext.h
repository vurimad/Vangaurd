/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redJobs2/include/jobWaitable.h"
#include "reflectionPool.h"

namespace job2
{
	class ScopedCounter;
}

namespace serialization
{

struct MemoryWaitContext
{
	RED_USE_MEMORY_POOL( red::PoolResource );

	MemoryWaitContext( const job2::ScopedCounter* inoutCounter, const job::Value< red::UniqueBuffer >& compressedData, const job::Value< red::UniqueBuffer >& decompressedData )
		: m_compressedData( compressedData )
		, m_decompressedData( decompressedData )
		, m_hasMemory( inoutCounter )
	{
	}

	job::Value< red::UniqueBuffer > m_compressedData;
	job::Value< red::UniqueBuffer > m_decompressedData;
	job2::JobWaitable m_hasMemory;
};

}
