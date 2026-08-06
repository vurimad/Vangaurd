/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerData.h"
#include "scriptDebuggerImplBase.h"

namespace comm
{
	class IProtoChannel;
}

namespace script
{
	enum class BreakpointType : Uint8;
}

// Script debugger, spawned when breakpoint occurs
class CScriptDebugger
{
private:
	red::SharedPtr< script::DebuggerData >		m_data;
	red::UniquePtr< CScriptDebuggerImplBase >	m_impl;

public:
	CScriptDebugger( comm::IProtoChannel& channel, Uint32 stoppedThreadId, red::ArraySpan< script::Thread >& threads, script::BreakpointType stopReason );
	~CScriptDebugger() = default;

	//! Process breakpoints, this is a loop
	void ProcessBreakpoint();
};
