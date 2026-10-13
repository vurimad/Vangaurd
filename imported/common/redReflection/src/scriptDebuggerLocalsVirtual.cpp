/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsVirtual.h"

namespace script { namespace debug {

Virtual::Virtual( const String& name, const String& value )
	: m_name( name )
	, m_value( value )
{

}

Virtual::~Virtual()
{

}

red::DynArray< IVariablePtr > Virtual::EnumerateChildren()
{
	return red::DynArray< IVariablePtr >(red::PoolDebug());
}

IVariablePtr Virtual::FindChild( const red::StringView& )
{
	return nullptr;
}

String Virtual::GetName() const
{
	return m_name;
}

const rtti::IType* Virtual::GetType() const
{
	return nullptr;
}

String Virtual::GetValue() const
{
	return m_value;
}

Uint32 Virtual::GetAttributes() const
{
	return RED_FLAG( Attributes::ReadOnly );
}

} } // namespace script { namespace debug {
