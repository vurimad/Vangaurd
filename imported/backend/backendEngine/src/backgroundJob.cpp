/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backgroundJob.h"

namespace tools
{

IBackgroundJob::IBackgroundJob()
	: m_refCount( 1 )
	, m_state( JS_Initialized )
{
}

IBackgroundJob::~IBackgroundJob()
{
	RED_FATAL_ASSERT( m_state.GetValue() != JS_Pending, "Trying to destroying a job while it's on the pending job queue. Crash will occur." );
}

void IBackgroundJob::AddRef()
{
	ASSERT( m_refCount.GetValue() > 0 );
	m_refCount.Increment();
}

void IBackgroundJob::Release()
{
	if ( 0 == m_refCount.Decrement() )
	{
		RED_DELETE( this );
	}
}

void IBackgroundJob::ScheduleJob()
{
	const EJobState newState = m_state.Increment();
	RED_TOUCH( newState );
	RED_FATAL_ASSERT( newState == JS_Pending, "" );
}

void IBackgroundJob::RunJob()
{
	RED_FATAL_ASSERT( m_state.GetValue() == JS_Pending, "" );
	const Bool result = Process();
	m_state.SetValue( result ? JS_Finished : JS_Failed );
}

}

