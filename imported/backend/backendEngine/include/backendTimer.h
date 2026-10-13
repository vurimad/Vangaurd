/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	/// A functionality that allows to call some magical code after a given amount of engine backend time
	/// Also, supports periodic checks
	/// NOT THREAD SAFE
	class BACKEND_ENGINE_API Timer
	{
	public:
		Timer( const AnsiChar* debugName );
		~Timer();

		typedef std::function< void() > TTimerFunction;

		/// Is the timer scheduled ?
		RED_FORCE_INLINE const Bool IsScheduled() const { return m_isScheduled; }

		/// Fire all pending timer
		static void FireAllPendingTimers();

		/// Stop timer
		void Stop();

		/// Schedule timer to be executed once after given amount of time
		/// Cancels any previous timer
		void ExecuteAfter( const Float time, TTimerFunction func );

		/// Schedule timer function to be called periodicaly
		/// Cancels any previous timer
		void ExecuteEvery( const Float time, TTimerFunction func );

	private:
		// debug only
		const AnsiChar*		m_debugName;

		// scheduling
		Bool				m_isScheduled;
		EngineTime			m_timeOfScheduledExecution;
		Float				m_period; // if >0 than the timer repreats
		TTimerFunction		m_func;

		// list of timers, only if actually waiting for something
		Timer*		m_next;
		Timer**		m_prev;

		static Timer*		st_list;
		static red::Mutex	st_listMutex;

		void RegisterForExecution();
		void UnregisterFromExecution();
	};

} // tools