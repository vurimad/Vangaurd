/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerImplBase.h"
#include "scriptDebuggerData.h"

#include "../../../common/commChannel/include/protoChannel.h"
namespace script
{
	enum class BreakpointType : Uint8;
}

class CScriptDebuggerImpl : public CScriptDebuggerImplBase, public comm::IProtoMessageListener
{
private:
	red::SharedPtr< script::DebuggerData > m_data;

	comm::IProtoChannel& m_channel;

public:
	CScriptDebuggerImpl( comm::IProtoChannel& channel, red::SharedPtr< script::DebuggerData > data, script::BreakpointType stopReason );
	virtual ~CScriptDebuggerImpl();

	// Inherited via CScriptDebuggerImplBase
	virtual Bool IsDebuggerConnected() const override;

	// Inherited via IChannelMessageListener
	virtual bool OnMessage( const comm::MessageSharedPtr& message, comm::IProtoResponse& response ) override;

private:
	comm::ScriptBreakExecuted ConstructBreakpointHitMessage( script::BreakpointType stopReason );
	
	void ConstructScriptThreadData( const script::Thread& in, comm::ScriptThread& out ) const;
	void ConstructLocals( const CScriptStackFrame* in, comm::ScriptStackFrame& out ) const;
	void ConstructLocals( const CScriptStackFrame* in, comm::ScriptLocals& out, const red::String& path ) const;
	void ConstructLocal( const CScriptStackFrame* in, comm::ScriptLocals& out, const red::String& path ) const;
};
