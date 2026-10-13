/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../redSystem/include/threads.h"
#include "scriptDebuggerImpl.h"

#include "scriptFile.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"

#include "scriptDebuggerLocalsFrame.h"
#include "scriptExpressionParserPath.h"

RED_NO_EMPTY_FILE();

#ifndef NO_SCRIPT_DEBUG

CScriptDebuggerImpl::CScriptDebuggerImpl( comm::IProtoChannel& channel, red::SharedPtr< script::DebuggerData > data, script::BreakpointType stopReason )
:	m_data( data )
,	m_channel( channel )
{
	m_channel.RegisterListener( this );

	comm::ScriptBreakExecuted message = ConstructBreakpointHitMessage( stopReason );
	m_channel.Send( message );

	RED_LOG( "Script breakpoint hit message sent: %u", red::ThreadId::CurrentThread().AsNumber() );
}

CScriptDebuggerImpl::~CScriptDebuggerImpl()
{
	m_channel.UnregisterListener( this );
}

Bool CScriptDebuggerImpl::IsDebuggerConnected() const
{
	return m_channel.IsConnected();
}

bool CScriptDebuggerImpl::OnMessage( const comm::MessageSharedPtr& message, comm::IProtoResponse& response )
{
	if ( message->IsA( comm::EMessageID::ScriptResumeExecution ) )
	{
		m_data->m_continue = true;
		return true;
	}
	else if ( message->IsA( comm::EMessageID::ScriptBreakRequest ) )
	{
		m_data->m_continue = true;
		return true;
	}
	else if( message->IsA( comm::EMessageID::ScriptLocalsRequest ) )
	{
		const comm::ScriptLocalsRequest* request = static_cast< const comm::ScriptLocalsRequest* >( message.Get() );

		script::Thread* thread = m_data->FindThread( request->threadId );

		if( thread )
		{
			CScriptStackFrame* frame = thread->m_stack[ request->stackId ];

			comm::ScriptLocals locals;

			if( request->children )
			{
				ConstructLocals( frame, locals, request->path );
			}
			else
			{
				ConstructLocal( frame, locals, request->path );
			}

			response.Send( locals );
		}
		else
		{
			RED_LOG_ERROR( "Could not find valid data for script thread with id: %u", request->threadId );
		}

		return true;
	}

	return false;
}

comm::ScriptBreakExecuted CScriptDebuggerImpl::ConstructBreakpointHitMessage( script::BreakpointType stopReason )
{
	comm::ScriptBreakExecuted message;

	switch( stopReason )
	{
	case script::BreakpointType::Breakpoint:
		message.reason = "Breakpoint";
	}

	message.breakpointThreadId = m_data->m_threadId;

	for( const script::Thread& thread : m_data->m_threads )
	{
		comm::ScriptThread messageData;

		messageData.id = thread.m_id;
		ConstructScriptThreadData( thread, messageData );

		message.threads.PushBack( messageData );
	}

	return message;
}

void CScriptDebuggerImpl::ConstructScriptThreadData( const script::Thread& in, comm::ScriptThread& out ) const
{
	const Uint32 numFrames = in.m_stack.Size();

	out.frames.Resize( numFrames );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		CScriptStackFrame* frame = in.m_stack[ i ];

		RED_FATAL_ASSERT( frame, "Callstack has null frames" );
		RED_FATAL_ASSERT( frame->m_function, "Callstack frame is missing rtti function" );

		if ( frame->m_function->GetClass() )
		{
			red::StringBuilder< String > func;
			func.Append( frame->m_function->GetClass()->GetName().AsChar() );
			func.Append( "::" );
			func.Append( frame->m_function->GetName().AsChar() );

			out.frames[ i ].func = func.ToString();
		}
		else
		{
			out.frames[ i ].func = frame->m_function->GetName().AsChar();
		}

		out.frames[ i ].id = i;
		out.frames[ i ].breakpoint.standard.file = frame->m_function->GetCode().GetScriptFile()->GetPath();
		out.frames[ i ].breakpoint.standard.line = frame->m_debugData.GetLine();
		out.frames[ i ].breakpoint.column = frame->m_debugData.GetColumn();
		out.frames[ i ].breakpoint.length = frame->m_debugData.GetLength();

		ConstructLocals( frame, out.frames[ i ] );
	}
}

void AddLocal( script::debug::IVariablePtr& source, const red::StringView& parentPath, red::DynArray< comm::ScriptLocal >& locals )
{
	String name = source->GetName();

	String path;
	if ( parentPath.Empty() )
	{
		path = name;
	}
	else if ( name[ 0 ] == '[' )
	{
		//Don't add a dot for array index specifiers
		path = String::Printf( "%.*hs%hs", parentPath.Length(), parentPath.Data(), name.AsChar() );
	}
	else
	{
		path = String::Printf( "%.*hs.%hs", parentPath.Length(), parentPath.Data(), name.AsChar() );
	}

	comm::ScriptLocal destination;
	destination.path = path;
	destination.name = name;
	destination.type = source->GetTypeName();
	destination.value = source->GetValue();
	destination.attributes = source->GetAttributes();

	locals.PushBack( destination );
}

void AddLocals( script::debug::IVariable* parent, const red::StringView& parentPath, red::DynArray< comm::ScriptLocal >& locals )
{
	auto children = parent->EnumerateChildren();

	for ( script::debug::IVariablePtr& child : children )
	{
		AddLocal( child, parentPath, locals );
	}
}

namespace script
{
	Bool ResolvePath( const CScriptStackFrame* in, const red::String& data, script::debug::IVariablePtr& out )
	{
		expression::Path path;
		
		if( !path.Build( data ) )
			return false;

		return path.Resolve( in, out );
	}
}

void CScriptDebuggerImpl::ConstructLocals( const CScriptStackFrame* in, comm::ScriptStackFrame& out ) const
{
	RED_FATAL_ASSERT( in != nullptr );

	script::debug::IVariablePtr top( RED_NEW( script::debug::Frame )( in ) );
	AddLocals( top.Get(), String::EMPTY(), out.locals );

	RED_LOG( "Constructed locals for frame: %hs(%hu)", in->m_function->GetName().AsChar(), in->m_debugData.GetLine() );
	for ( Uint32 i = 0; i < out.locals.Size(); ++i )
	{
		RED_LOG( "-- %hs = %hs", out.locals[ i ].path.AsChar(), out.locals[ i ].value.AsChar() );
	}
}

void CScriptDebuggerImpl::ConstructLocals( const CScriptStackFrame* in, comm::ScriptLocals& out, const red::String& path ) const
{
	RED_FATAL_ASSERT( in != nullptr );

	script::debug::IVariablePtr requested;
	if( script::ResolvePath( in, path, requested ) )
	{
		AddLocals( requested.Get(), path, out.locals );

		RED_LOG( "Constructed locals for path request \"%.*hs\": %hs(%hu)", path.Length(), path.Data(), in->m_function->GetName().AsChar(), in->m_debugData.GetLine() );
		for( Uint32 i = 0; i < out.locals.Size(); ++i )
		{
			RED_LOG( "-- %hs = %hs", out.locals[ i ].path.AsChar(), out.locals[ i ].value.AsChar() );
		}
	}
}

void CScriptDebuggerImpl::ConstructLocal( const CScriptStackFrame* in, comm::ScriptLocals& out, const red::String& path ) const
{
	RED_FATAL_ASSERT( in != nullptr );

	script::debug::IVariablePtr requested;
	if ( script::ResolvePath( in, path, requested ) )
	{
		red::StringView parentPath = path;

		parentPath.TrimBack( requested->GetName().Length() );
		while ( parentPath.Length() > 0 && parentPath.Back() == '.' )
			parentPath.TrimBack( 1 );

		AddLocal( requested, parentPath, out.locals );
	}
}

#endif
