/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"

#include "../../redSystem/include/threads.h"

#include "scriptingSystem.h"
#include "scriptingSystemImpl.h"
#include "scriptDebugger.h"
#include "scriptDebuggerImpl.h"
#include "scriptStackFrame.h"
#include "scriptFile.h"

namespace script {

Thread::Thread()
	: m_id( 0 )
{
}

void Thread::SetStack( CScriptStackFrame& stack )
{
	// Go to parent stack frames building the callstack
	CScriptStackFrame* frame = &stack;
	while ( frame )
	{
		// Add to stack list
		m_stack.PushBack( frame );

		// Go to base frame
		frame = frame->GetParent();
	}
}

//////////////////////////////////////////////////////////////////////////

DebuggerData::DebuggerData( Uint32 stoppedThreadId, red::ArraySpan< Thread >& threads )
:	m_continue( false )
,	m_threadId( stoppedThreadId )
,	m_threads( threads )
{
}

} // namespace script {

#ifndef NO_SCRIPT_DEBUG


//////////////////////////////////////////////////////////////////////////

CScriptDebugger::CScriptDebugger( comm::IProtoChannel& channel, Uint32 stoppedThreadId, red::ArraySpan< script::Thread >& threads, script::BreakpointType stopReason )
{
	m_data.Reset( RED_NEW( script::DebuggerData )( stoppedThreadId, threads ) );
	m_impl.Reset( RED_NEW( CScriptDebuggerImpl )( channel, m_data, stopReason ) );
}

void CScriptDebugger::ProcessBreakpoint()
{
#ifdef RED_PLATFORM_WINPC
	::ReleaseCapture();
	::ClipCursor( nullptr );
	while ( ::ShowCursor( true ) < 0 );
#endif

	RED_LOG( "Script processing breakpoint for thread: %u", red::ThreadId::CurrentThread().AsNumber() );

	// While not requested to continue
	while ( !m_data->m_continue )
	{
		// Do not eat all the CPU
		red::SleepOnCurrentThread( 30 );

		if( !m_impl->IsDebuggerConnected() )
		{
			m_data->m_continue = true;
			IScriptingSystem::GetInstance().DisableAllBreakpoints();
		}
	}

	RED_LOG( "Script finished processing breakpoint for thread: %u", red::ThreadId::CurrentThread().AsNumber() );
}

#endif
