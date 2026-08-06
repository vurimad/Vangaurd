/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptFile.h"

CScriptFile::CScriptFile()
	: m_pathHash( 0 )
	, m_sourceCRC( 0 )
	, m_path( red::String::EMPTY() )
	, m_breakpoints( red::PoolScript() )
#ifdef USE_PROFILER
	, m_instrumentationObjects( red::PoolScript() )
#endif
{
}

void CScriptFile::AddBreakpoint( const script::RuntimeBreakpoint& breakpoint )
{
	// Breakpoints are all added at once, and since red::Map makes sure to
	// clean [sort] the data before an access, we can use InsertUnsorted
	m_breakpoints.InsertUnsorted( breakpoint.GetEndPosition(), breakpoint );
}

void CScriptFile::SortBreakpoints()
{
	m_breakpoints.MakeClean();
}

void CScriptFile::ClearBreakpoints()
{
	m_breakpoints.Clear();
}

script::RuntimeBreakpoint* CScriptFile::FindBreakpoint( Uint32 position )
{
	using Iter = red::ArraySpanIterator< red::ArraySpan< const Uint32 > >;

	red::ArraySpan< const Uint32 > keys = m_breakpoints.Keys();
	Iter start = keys.begin();
	Iter end = keys.end();

	Iter result = std::lower_bound( start, end, position );

	if ( result != end )
	{
		return &m_breakpoints[ *result ];
	}

	return nullptr;
}

script::BreakpointResult CScriptFile::SetBreakpoint( Uint32 position, Bool isSet )
{
	script::RuntimeBreakpoint* breakpoint = FindBreakpoint( position );

	if( breakpoint )
	{
		breakpoint->Set( isSet );
	}

	return script::BreakpointResult( breakpoint );
}

red::DynArray< script::BreakpointResult > CScriptFile::DisableAllBreakpoints()
{
	red::DynArray< script::BreakpointResult > results{ red::PoolDebug() };

	for( auto breakpoint : m_breakpoints )
	{
		if( breakpoint.Value().IsSet() )
		{
			breakpoint.Value().Set( false );
			results.PushBack( script::BreakpointResult( &breakpoint.Value() ) );
		}
	}

	return results;
}

#ifdef USE_PROFILER
void CScriptFile::AddInstrumentationObject( Int32 codeOffset, rtti::Function* function )
{
	m_instrumentationObjects.PushBack( red::CreateSharedPtr< script::RuntimeInstrumentationObject, red::PoolDebug >( codeOffset, function ) );
}

void CScriptFile::ClearInstrumentationObjects()
{
	m_instrumentationObjects.Clear();
}

#endif 
