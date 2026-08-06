/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobStackTrace.h"

namespace job
{
	StackTraceHandle::StackTraceHandle( const StackTraceCacheEntryPath* path )
		: m_path( path )
	{
		if ( !m_path )
		{
			m_path = &s_nullPath;
		}
	}

	StackTraceHandle::StackTraceHandle()
	{
		m_path = &s_nullPath;
	}

	const job::StackTraceCacheEntryPath StackTraceHandle::s_nullPath = { {}, { "Run with '-jobDebugger' on the commandline to get stacktraces" } };
}