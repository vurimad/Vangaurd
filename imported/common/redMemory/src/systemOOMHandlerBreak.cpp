/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "systemOOMHandlerBreak.h"
#include "assert.h"
#include "reporter.h"
#include "threadIdProvider.h"
#include "utils.hpp"
#include "../include/allocatorMetricsLogger.h"

namespace red
{
namespace memory
{
	SystemOOMHandlerBreak::SystemOOMHandlerBreak()
		: m_reporter( nullptr )
	{}

	SystemOOMHandlerBreak::~SystemOOMHandlerBreak()
	{}

	void SystemOOMHandlerBreak::Initialize( const Reporter * reporter )
	{
		m_reporter = reporter;
	}

	void SystemOOMHandlerBreak::OnHandleProxyAllocateFailure( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment )
	{
		if ( m_reporter )
		{
			m_reporter->SaveAllocatorOOMCrashData( proxyId, size, alignment );
			m_reporter->WriteReportToJson();
			m_reporter->WriteAllocatorOOMReportToLog( proxyId, proxy, size, alignment );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Out of Memory! Failed to allocate %" PRIu32 " bytes with alignment %" PRIu32 ".", size, alignment );
	}

	void SystemOOMHandlerBreak::OnHandleSystemCommitFailure( u64 size, u32 alignment )
	{
		if ( m_reporter )
		{
			m_reporter->SaveSystemCommitOOMCrashData( size, alignment );
			m_reporter->WriteReportToJson();
			m_reporter->WriteSystemCommitOOMReportToLog( size, alignment );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Out of Memory! Failed to allocate %" PRIu32 " bytes with alignment %" PRIu32 ".", size, alignment );
	}

	void SystemOOMHandlerBreak::OnHandleSystemReservePagesFailure( u64 size, u32 alignment, u32 pageSize )
	{
		if ( m_reporter )
		{
			m_reporter->SaveSystemReserveOOMCrashData( size, alignment, pageSize );
			m_reporter->WriteReportToJson();
			m_reporter->WriteSystemReserveOOMReportToLog( size, alignment, pageSize );
		}

		ALWAYSENABLED_RED_MEMORY_FATAL( "Out of Memory! Failed to allocate %" PRIu32 " bytes with alignment %" PRIu32 ".", size, alignment );
	}
}
}