
/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptExpressionParserEnumVariable.h"
#include "rttiEnum.h"

namespace script { namespace expression {
EnumVariable::EnumVariable( const rtti::EnumType* type, const red::StringView& option )
	: m_type( type )
{
	m_option = RED_NAME( option );
}

EnumVariable::EnumVariable( const rtti::EnumType* type, const CName& option )
	: m_option( option )
	, m_type( type )
{
}

EnumVariable::EnumVariable( const rtti::EnumType* type )
	: m_type( type )
{
}

String EnumVariable::GetName() const
{
	CName name = m_option.Empty()? m_type->GetName() : m_option;

	red::StringView nameStr = name.AsStringView();
	return String( nameStr.Data(), nameStr.Length() );
}

const rtti::IType* EnumVariable::GetType() const
{
	return m_type;
}

String EnumVariable::GetValue() const
{
	if( m_option.Empty() )
	{
		return "Enum Type";
	}

	red::StringView optionStr = m_option.AsStringView();
	return String( optionStr.Data(), optionStr.Length() );
}

debug::IVariablePtr EnumVariable::FindChild( const red::StringView& name )
{
	if( !m_option.Empty() )
		return nullptr;

	CName option = RED_NAME_NOREG( name );

	Int64 value;
	if( m_type->FindValue( option, value ) )
	{
		return red::CreateUniquePtr< EnumVariable >( m_type, option );
	}

	return nullptr;
}

red::DynArray< debug::IVariablePtr > EnumVariable::EnumerateChildren()
{
	red::DynArray< debug::IVariablePtr > children{ red::PoolScript() };

	if( m_option.Empty() )
	{
		auto options = m_type->GetOptions();

		for( const CName& option : options )
		{
			red::UniquePtr< EnumVariable > child = red::CreateUniquePtr< EnumVariable >( m_type, option );
			children.PushBack( std::move( child ) );
		}
	}

	return children;
}

Uint32 EnumVariable::GetAttributes() const
{
	return RED_FLAG( debug::Attributes::ReadOnly ) | ( m_option.Empty()? RED_FLAG( debug::Attributes::IsExpandable ) : 0 );
}

} } // namespace script { namespace expression {
