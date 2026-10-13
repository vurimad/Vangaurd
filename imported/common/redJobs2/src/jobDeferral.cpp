/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "jobDeferral.h"
#include "jobDispatcher.h"
#include "jobDebugger.h"

namespace job
{

namespace prv
{
	extern Dispatcher* gDispatcher;
}

void CompletionDeferral::Swap( CompletionDeferral& rhs )
{
	if ( this != &rhs ) // checking to avoid double (and unneccesary) job debugger registration/unregistration
	{
		UnregisterFromDebugger();
		rhs.UnregisterFromDebugger();

		::Swap( m_debugName, rhs.m_debugName );
		::Swap( m_debugUserData, rhs.m_debugUserData );
		::Swap( m_counterEntry, rhs.m_counterEntry );
		const Bool tmpIsFinished = rhs.m_isFinished.Exchange( m_isFinished.GetValue() );
		m_isFinished.SetValue( tmpIsFinished );

		RegisterWithDebugger();
		rhs.RegisterWithDebugger();
	}
}

void CompletionDeferral::RegisterWithDebugger()
{
	if ( m_counterEntry && prv::gDispatcher->GetDebugger() )
	{
		prv::gDispatcher->GetDebugger()->RegisterDeferral( *this );
	}
}

void CompletionDeferral::UnregisterFromDebugger()
{
	if ( m_counterEntry && prv::gDispatcher->GetDebugger() )
	{
		prv::gDispatcher->GetDebugger()->UnregisterDeferral( *this );
	}
}

CompletionDeferral::CompletionDeferral( const char* debugName, const void* debugUserData, prv::CounterEntry& counter )
	: m_debugName( debugName )
	, m_debugUserData( debugUserData )
	, m_counterEntry( &counter )
{
	RED_FATAL_ASSERT( m_counterEntry );
	if ( prv::gDispatcher->GetDebugger() )
	{
		prv::gDispatcher->GetDebugger()->RegisterDeferral( *this );
	}
}

CompletionDeferral::CompletionDeferral( CompletionDeferral&& other )
	: CompletionDeferral()
{
	Swap( other );
 	RED_FATAL_ASSERT( !m_isFinished.GetValue(), "Moved from a finished deferral. Asserting since likely a usage error" );
}

CompletionDeferral::CompletionDeferral()
	: m_debugName( "" )
	, m_debugUserData( nullptr )
	, m_counterEntry( nullptr )
	, m_isFinished( false )
{
}

CompletionDeferral& CompletionDeferral::operator=( CompletionDeferral&& rhs )
{
	Swap( rhs );
	RED_FATAL_ASSERT( !m_isFinished.GetValue(), "Moved from a finished deferral. Asserting since likely a usage error" );

	return *this;
}

Bool CompletionDeferral::TryFinishDeferral()
{
	if ( m_isFinished.Exchange( true ) )
	{
		return false;
	}

	if ( m_counterEntry )
	{
		if ( prv::gDispatcher->GetDebugger() )
		{
			prv::gDispatcher->GetDebugger()->UnregisterDeferral( *this );
		}

		prv::gDispatcher->DecrementCounterEntryInternal( m_counterEntry, nullptr );
		m_counterEntry = nullptr;
	}

	return true;
}

CompletionDeferral::~CompletionDeferral()
{
	(void)TryFinishDeferral();
}

void CompletionDeferral::FinishDeferral()
{
	if ( !TryFinishDeferral() )
	{
		RED_FATAL( "Deferral already finished! Double finish attempt?" );
	}
}

}
