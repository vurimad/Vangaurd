/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptBreakpointRuntime.h"
#include "scriptOpcodesList.h"
#include "scriptBreakpointCondition.h"

namespace script {

RuntimeBreakpoint::RuntimeBreakpoint( Uint32 codeOffset, Uint32 lineStart, Uint16 breakpointOffset, Uint16 length, Uint16 line )
	: m_function( nullptr )
	, m_codeOffset( codeOffset )
	, m_lineStart( lineStart )
	, m_breakpointOffset( breakpointOffset )
	, m_length( length )
	, m_line( line )
{
}

RuntimeBreakpoint::RuntimeBreakpoint()
	: RuntimeBreakpoint( 0, 0, 0, 0, 0 )
{
}

const Uint8* RuntimeBreakpoint::GetToggleByte() const
{
	RED_FATAL_ASSERT( m_function, "Breakpoint data missing associated function, check CScriptDataBinder::LoadOpcodes" );

	CScriptCompiledCode& code = m_function->GetCode();

	Uint8* data = static_cast<Uint8*>( code.GetCode() ) + m_codeOffset;
	RED_FATAL_ASSERT( *data == OP_Breakpoint, "Compiled script code is invalid" );
	++data;

	Uint16 line;
	Uint32 start;		// Start of Line
	Uint16 offset;		// "Column"
	Uint16 length;

	// Line
	red::Memcpy( &line, data, sizeof( line ) );
	data += sizeof( line );

	// Line position
	red::Memcpy( &start, data, sizeof( start ) );
	data += sizeof( start );

	// "Column"
	red::Memcpy( &offset, data, sizeof( offset ) );
	data += sizeof( offset );

	// Length
	red::Memcpy( &length, data, sizeof( length ) );
	data += sizeof( length );

	RED_FATAL_ASSERT( line == m_line );
	RED_FATAL_ASSERT( start == m_lineStart );
	RED_FATAL_ASSERT( offset == m_breakpointOffset );
	RED_FATAL_ASSERT( length == m_length );

	return data;
}

breakpoint::Condition* RuntimeBreakpoint::GetCondition( const Uint8* toggleByte )
{
	breakpoint::Condition* condition = nullptr;
	red::Memcpy( &condition, toggleByte + 1, sizeof( breakpoint::Condition* ) );

	return condition;
}

breakpoint::Condition* RuntimeBreakpoint::GetCondition()
{
	return GetCondition( GetToggleByte() );
}

breakpoint::Condition* RuntimeBreakpoint::GetOrCreateCondition()
{
	Uint8* toggleByte = GetToggleByte();

	breakpoint::Condition* condition = GetCondition( toggleByte );

	if ( !condition )
	{
		Uint8* data = toggleByte + 1;

		condition = RED_NEW( breakpoint::Condition );
		red::Memcpy( data, &condition, sizeof( breakpoint::Condition* ) );
	}

	return condition;
}

Bool RuntimeBreakpoint::IsSet() const
{
	const Uint8* data = GetToggleByte();

	return *data != 0u;
}

void RuntimeBreakpoint::Set( Bool isSet )
{
	Uint8* data = GetToggleByte();

	*data = isSet ? 1u : 0u;

	//
	if( !isSet )
	{
		ClearCondition( data );
	}
}

void RuntimeBreakpoint::ClearCondition( Uint8* toggleByte )
{
	breakpoint::Condition* condition = GetCondition( toggleByte );

	if( condition )
	{
		RED_DELETE( condition );

		Uint8* conditionData = toggleByte + 1;
		red::Memzero( conditionData, sizeof( breakpoint::Condition* ) );
	}
}

void RuntimeBreakpoint::ClearCondition()
{
	ClearCondition( GetToggleByte() );
}

void RuntimeBreakpoint::SetPasscount( Uint32 passcount, Uint32 style )
{
	breakpoint::Condition* condition = GetOrCreateCondition();
	RED_FATAL_ASSERT( condition );

	condition->SetPassCount( passcount, static_cast< breakpoint::PassStyle >( style ) );
}

void RuntimeBreakpoint::SetHitcount( Uint32 hitcount )
{
	breakpoint::Condition* condition = GetCondition();

	if( condition )
	{
		condition->SetHitCount( hitcount );
	}
}

void RuntimeBreakpoint::SetCondition( const String& expression, Uint32 style )
{
	breakpoint::Condition* condition = GetOrCreateCondition();
	RED_FATAL_ASSERT( condition );

	condition->SetExpression( expression, static_cast< breakpoint::ConditionStyle >( style ) );
}

} // namespace script {
