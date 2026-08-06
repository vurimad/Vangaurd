/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptBreakpointResult.h"
#include "scriptBreakpointRuntime.h"

namespace script {

BreakpointResult::BreakpointResult( const RuntimeBreakpoint* source )
	: m_breakpoint( source )
{
}

BreakpointResult::BreakpointResult( const BreakpointResult& other )
	: m_breakpoint( other.m_breakpoint )
{
}

Bool BreakpointResult::Success() const
{
	return m_breakpoint != nullptr;
}

Uint16 BreakpointResult::GetLine() const
{
	return m_breakpoint->GetLine();
}

Uint32 BreakpointResult::GetLineStart() const
{
	return m_breakpoint->GetLineStart();
}

Uint16 BreakpointResult::GetColumn() const
{
	return m_breakpoint->GetColumn();
}

Uint16 BreakpointResult::GetLength() const
{
	return m_breakpoint->GetLength();
}

} // namespace script {
