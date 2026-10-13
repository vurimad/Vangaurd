/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptBreakpointCondition.h"

#include "scriptStackFrame.h"
#include "scriptExpressionParserPath.h"
#include "scriptDebuggerLocalsBase.h"

namespace script { namespace breakpoint {

static_assert( sizeof( intptr_t ) == sizeof( Condition* ), "If this assert fails then my understanding of intptr_t is incorrect" );

Condition::Condition() = default;
Condition::~Condition() = default;

void Condition::SetPassCount( Uint32 count, PassStyle style )
{
	if( !m_passcount )
	{
		m_passcount = red::CreateUniquePtr< PassCountImpl >();
	}

	m_passcount->SetPassCount( count, style );
}

void Condition::SetHitCount( Uint32 hitcount )
{
	if( !m_passcount )
		return;

	m_passcount->SetHitCount( hitcount );
}

void Condition::SetExpression( const String& condition, ConditionStyle style )
{
	switch( style )
	{
	case ConditionStyle::None:
		m_isTrue.Reset();
		break;

	case ConditionStyle::IsTrue:
		if( !m_isTrue )
			m_isTrue = red::CreateUniquePtr< IsTrueImpl >();

		m_isTrue->SetExpression( condition );
		break;
	}
}

Bool Condition::Evaluate( const CScriptStackFrame& callstack )
{
	if( m_passcount && !m_passcount->Evaluate() )
		return false;

	if( m_isTrue && !m_isTrue->Evaluate( callstack ) )
		return false;

	return true;
}

//////////////////////////////////////////////////////////////////////////

PassCountImpl::PassCountImpl()
	: m_passcount( 0 )
	, m_hitcount( 0 )
	, m_passStyle( PassStyle::None )
{
}

PassCountImpl::~PassCountImpl() = default;

void PassCountImpl::SetPassCount( Uint32 count, PassStyle style )
{
	m_passcount = count;
	m_passStyle = style;
}

void PassCountImpl::SetHitCount( Uint32 hitcount )
{
	m_hitcount = hitcount;
}

Bool PassCountImpl::Evaluate()
{
	++m_hitcount;

	switch ( m_passStyle )
	{
	case PassStyle::None:
		return true;

	case PassStyle::Equal:
		return m_hitcount == m_passcount;

	case PassStyle::EqualOrGreater:
		return m_hitcount >= m_passcount;

	case PassStyle::Mod:
		return m_hitcount % m_passcount == 0;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////

IsTrueImpl::IsTrueImpl() = default;
IsTrueImpl::~IsTrueImpl() = default;

void IsTrueImpl::SetExpression( const String& expression )
{
	m_expression = expression;

	m_path.Clear();
	m_path.Build( m_expression );
}

Bool IsTrueImpl::Evaluate( const CScriptStackFrame& callstack )
{
	debug::IVariablePtr result;
	if( !m_path.Resolve( &callstack, result ) )
		return false;

	return result->GetValue().EqualsNC( "true" );
}

} } // namespace script { namespace breakpoint {
