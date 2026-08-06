/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptDebuggerLocalsSimple.h"
#include "rttiType.h"

namespace script { namespace debug {

Simple::Simple( const void* data, const rtti::IType* type, const String& name )
	: m_type( type )
	, m_data( data )
	, m_name( name )
{
}

String Simple::GetName() const
{
	return m_name;
}

const rtti::IType* Simple::GetType() const
{
	return m_type;
}

String Simple::GetValue() const
{
	String text;
	if ( m_type->ToString( m_data, text ) )
		return text;

	return "<Unknown>";
}

Uint32 Simple::GetAttributes() const
{
	return RED_FLAG( Attributes::ReadOnly ) | RED_FLAG( Attributes::TypeProperty );
}

const void* Simple::GetRaw() const
{
	return m_data;
}

} } // namespace script { namespace debug {
