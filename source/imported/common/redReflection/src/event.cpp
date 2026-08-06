/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "event.h"
#include "rttiClassBuilder.h"
#include "scriptStackFrame.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( Event, red );
RTTI_PARENT_TYPE( IScriptable );
RTTI_SCRIPT_ALIAS( "Event" );
RTTI_END_TYPE();


namespace red
{
	Event::Event()
	{
#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
		red::Memzero( &m_senderInfo.m_codeStacktrace.m_frames, sizeof( m_senderInfo.m_codeStacktrace.m_frames ) );
		red::Memzero( &m_senderInfo.m_scriptsStacktrace, sizeof( m_senderInfo.m_scriptsStacktrace ) );
#endif
	}

	Event::~Event()
	{
	}

	Uint16 Event::GetEventTypeID() const
	{
		return GetClass()->GetEventClassId();
	}

#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
	void Event::DEBUG_CaptureCodeStacktrace()
	{
		if( m_senderInfo.m_codeStacktrace.m_framesCount > 0 ) // code stack trace was filled once, so we wipe the data
		{
			red::Memzero( &m_senderInfo.m_codeStacktrace, sizeof( m_senderInfo.m_codeStacktrace ) );
		}

		const Uint32 numFramesToSkip = 1; // Skip Event::DEBUG_CaptureCodeStacktrace(...) function

		dbgutils::StackTrace stackTrace;
		if( dbgutils::GetStackBackTrace_Profiler( numFramesToSkip, stackTrace ) )
		{
			m_senderInfo.m_codeStacktrace.m_framesCount = Min( stackTrace.m_numFrameAddresses, red::Event::SenderInfo::CodeStacktrace::c_framesSize );
			for( Uint32 i = 0; i < m_senderInfo.m_codeStacktrace.m_framesCount; ++i )
			{
				m_senderInfo.m_codeStacktrace.m_frames[ i ] = reinterpret_cast< const void* >( stackTrace.m_frameAddress[ i ].m_absoluteVirtualAddress );
			}
		}
	}
#endif

#ifdef RED_EVENT_SENDER_INFO_AVAILABLE
	void Event::DEBUG_CaptureScriptsStacktrace( CScriptStackFrame& stack )
	{
		if( m_senderInfo.m_scriptsStacktrace[ 0 ].m_function != nullptr ) // scripts stack trace was filled once, so we wipe the data
		{
			red::Memzero( &m_senderInfo.m_scriptsStacktrace, sizeof( m_senderInfo.m_scriptsStacktrace ) );
		}

		Uint32 currentFunctionIndex = 0;
		for( const CScriptStackFrame* stackFrame = &stack; stackFrame; stackFrame = stackFrame->m_parent )
		{
			if( currentFunctionIndex >= red::Event::SenderInfo::c_scriptsStacktraceSize )
			{
				break;
			}

			if( !stackFrame->m_function )
			{
				continue;
			}

			red::Event::SenderInfo::ScriptsStacktrace* const scriptsStacktrace = &m_senderInfo.m_scriptsStacktrace[ currentFunctionIndex++ ];

			if( const rtti::ClassType* const cls = stackFrame->m_function->GetClass() )
			{
				scriptsStacktrace->m_class = cls->GetName().AsChar();
			}

			scriptsStacktrace->m_function = stackFrame->m_function->GetFamilyName().AsChar();
			scriptsStacktrace->m_line = stackFrame->m_function->GetCode().GetSourceLine();
		}
	}
#endif
}
