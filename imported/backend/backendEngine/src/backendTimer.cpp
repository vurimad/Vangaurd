/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#pragma hdrstop

#include "backendTimer.h"

namespace tools
{
	Timer* Timer::st_list = nullptr;
	red::Mutex	Timer::st_listMutex;

	Timer::Timer( const AnsiChar* debugName )
		: m_debugName( debugName )
		, m_isScheduled( false )
		, m_period( 0.0f )
		, m_next( nullptr )
		, m_prev( nullptr )
	{}

	Timer::~Timer()
	{
		if ( m_isScheduled )
			UnregisterFromExecution();

		RED_FATAL_ASSERT( m_prev == nullptr, "Destroying timer that is still in the list" );
		RED_FATAL_ASSERT( m_next == nullptr, "Destroying timer that is still in the list" );
	}

	void Timer::FireAllPendingTimers()
	{
		PC_SCOPE( BackendTimers );

		RED_FATAL_ASSERT( ::SIsMainThread(), "Backend timers are for main thread only" );

		red::ScopedLock< red::Mutex > lock( st_listMutex );

		const EngineTime currentTime = EngineTime::GetNow();
		for ( auto* ptr = st_list; ptr; )
		{
			auto* next = ptr->m_next;

			// process timer
			if ( currentTime >= ptr->m_timeOfScheduledExecution )
			{
				RED_FATAL_ASSERT( ptr->m_func, "Timer with not function" );

				// timer callback
				ptr->m_func();

				// delete or reschedule
				if ( ptr->m_period >= 0.0f )
				{
					ptr->m_timeOfScheduledExecution = currentTime + ptr->m_period;
				}
				else
				{
					ptr->UnregisterFromExecution();
				}
			}

			ptr = next;
		}
	}

	void Timer::Stop()
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Backend timers are for main thread only" );

		if ( m_isScheduled )
		{
			UnregisterFromExecution();
			m_period = 0.0f;
		}
	}

	void Timer::ExecuteAfter( const Float time, TTimerFunction func )
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Backend timers are for main thread only" );

		m_timeOfScheduledExecution = EngineTime::GetNow() + time;
		m_period = 0.0f;
		m_func = func;

		if ( !m_isScheduled )
			RegisterForExecution();
	}

	void Timer::ExecuteEvery( const Float time, TTimerFunction func )
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Backend timers are for main thread only" );

		m_timeOfScheduledExecution = EngineTime::GetNow() + time;
		m_period = time;
		m_func = func;

		if ( !m_isScheduled )
			RegisterForExecution();
	}

	void Timer::RegisterForExecution()
	{
		RED_FATAL_ASSERT( !m_isScheduled, "Invalid state" );

		{
			red::ScopedLock< red::Mutex > lock( st_listMutex );

			m_next = st_list;
			if ( st_list ) st_list->m_prev = &m_next;
			m_prev = &st_list;
			st_list = this;
		}

		m_isScheduled = true;
	}

	void Timer::UnregisterFromExecution()
	{
		RED_FATAL_ASSERT( m_isScheduled, "Invalid state" );

		{
			red::ScopedLock< red::Mutex > lock( st_listMutex );

			*m_prev = m_next;
			if ( m_next )
				m_next->m_prev = m_prev;

			m_next = nullptr;
			m_prev = nullptr;
		}

		m_isScheduled = false;
	}

} // tools