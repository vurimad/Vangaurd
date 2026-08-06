/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptExpressionParserGeneratedVariable.h"

namespace script { namespace expression
{
	GeneratedVariable::GeneratedVariable( const String& value, const rtti::IType* type )
		: m_value( value )
		, m_type( type )
	{
	}

	GeneratedVariable::GeneratedVariable( const String& value, const String& typeName )
		: m_value( value )
		, m_typeName( typeName )
		, m_type( nullptr )
	{
	}

	String GeneratedVariable::GetName() const
	{
		return m_value;
	}

	const rtti::IType* GeneratedVariable::GetType() const
	{
		return m_type;
	}

	String GeneratedVariable::GetValue() const
	{
		return m_value;
	}

	String GeneratedVariable::GetTypeName() const
	{
		if( !m_typeName.Empty() )
			return m_typeName;

		return TBaseClass::GetTypeName();

	}

} } // namespace script { namespace expression
