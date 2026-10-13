/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsFrame.h"

#include "scriptDebuggerLocalsObject.h"
#include "scriptDebuggerLocalsProperty.h"

#include "scriptStackFrame.h"
#include "scriptable.h"

namespace script { namespace debug {

Frame::Frame( const CScriptStackFrame* frame )
	: m_frame( frame )
{

}

Frame::~Frame()
{

}

red::DynArray< IVariablePtr > Frame::EnumerateChildren()
{
	const IScriptable* context = m_frame->GetContext();

	red::DynArray< const rtti::Property* > properties{ red::PoolDebug() };
	m_frame->m_function->GetProperties( properties );

	const Uint32 count = properties.Size() + ( context ? 1 : 0 );

	red::DynArray< IVariablePtr > out{ red::PoolDebug() };
	out.Reserve( count );

	if ( context )
	{
		IVariablePtr ptr( RED_NEW( AliasedObject )( context, "this" ) );
		out.PushBack( std::move( ptr ) );
	}

	for ( const rtti::Property* property : properties )
	{
		const void* data = property->GetOffsetPtr( property->IsFuncLocal() ? m_frame->m_locals : m_frame->m_params );

		IVariablePtr ptr( RED_NEW( Property )( property, data ) );
		out.PushBack( std::move( ptr ) );
	}

	return out;
}

IVariablePtr Frame::FindChild( const red::StringView& name )
{
	if ( name == "this" && m_frame->GetContext() )
	{
		return IVariablePtr( RED_NEW( AliasedObject )( m_frame->GetContext(), "this" ) );
	}

	const rtti::Property* property = m_frame->m_function->FindProperty( RED_NAME_NOREG( name ) );

	if( property )
	{
		const void* data = property->GetOffsetPtr( property->IsFuncLocal() ? m_frame->m_locals : m_frame->m_params );
		return IVariablePtr( RED_NEW( Property )( property, data ) );
	}

	return nullptr;
}

String Frame::GetName() const
{
	return String::EMPTY();
}

const rtti::IType* Frame::GetType() const
{
	return nullptr;
}

String Frame::GetValue() const
{
	return String::EMPTY();
}

} } // namespace script { namespace debug {
