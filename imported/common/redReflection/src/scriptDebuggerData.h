/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptable.h"

namespace script
{
	struct Thread
	{
		Uint32 m_id;
		red::DynArray< CScriptStackFrame* >	m_stack{ red::PoolDebug() };	//!< Stack frames ( callstack )

		Thread();
		void SetStack( CScriptStackFrame& stack );
	};

	struct DebuggerData
	{
		RED_USE_MEMORY_POOL( red::PoolScript );

		Bool m_continue;								//!< Continue execution
		Uint32 m_threadId;								//!< Id of the thread that encountered the breakpoint
		red::ArraySpan< Thread > m_threads;

		DebuggerData( Uint32 stoppedThreadId, red::ArraySpan< Thread >& threads );
		~DebuggerData() = default;

		Thread* FindThread( Uint32 id );
	};

	RED_INLINE Thread* DebuggerData::FindThread( Uint32 id )
	{
		for( Thread& thread : m_threads )
		{
			if( thread.m_id == id )
				return &thread;
		}

		return nullptr;
	}
}
