/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "oomHandlerBreak.h"
#include "assert.h"
#include "reporter.h"
#include "threadIdProvider.h"
#include "utils.hpp"

namespace red
{
namespace memory
{
	PoolOOMHandlerBreak::PoolOOMHandlerBreak()
		: m_reporter( nullptr )
	{}

	PoolOOMHandlerBreak::~PoolOOMHandlerBreak()
	{}

	void PoolOOMHandlerBreak::Initialize( const Reporter * reporter )
	{
		m_reporter = reporter;
	}

	void PoolOOMHandlerBreak::OnHandlePoolAllocateFailure( const char * poolName, const char * allocatorName, u32 size, u32 alignment )
	{
		if ( m_reporter )
		{
			m_reporter->SavePoolOOMCrashData( poolName, allocatorName, size, alignment );
			m_reporter->WriteReportToJson();
			m_reporter->WritePoolOOMReportToLog( poolName, allocatorName, size, alignment );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Out of Memory! Failed to allocate %" PRIu32 " bytes with alignment %" PRIu32 " from pool '%s' using '%s'", size, alignment, poolName, allocatorName );
	
		RED_UNUSED( poolName );
		RED_UNUSED( size );
		RED_UNUSED( alignment );
	}

}
}
