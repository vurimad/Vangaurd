/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/staticArray.h"
#include "../../redSystem/include/dbgUtils.h"
#include "redJobs2Api.h"

namespace job
{

struct StackTraceCacheEntry
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

	dbgutils::StackTrace stackTrace;
	red::String debugString;
};

struct StackTraceCacheEntryPath
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

	static const Uint32 c_maxChainTraceLevels = 8;

	red::StaticArray< const StackTraceCacheEntry*, c_maxChainTraceLevels > stackTraces;
	red::StaticArray< const char*, c_maxChainTraceLevels > debugStringView;
};

class REDJOBS2_API StackTraceHandle
{
public:
	explicit StackTraceHandle( const StackTraceCacheEntryPath* path );

	StackTraceHandle();

	const StackTraceCacheEntryPath& GetPath() const { return *m_path;  }

private:
	static const StackTraceCacheEntryPath s_nullPath;
	const StackTraceCacheEntryPath* m_path;
};


}
