/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "enumInternalBuilder.h"
#include "rttiInternalTypeName.h"
#include "rttiRegistration.h"

//////////////////////////////////////////////////////////////////////////
// Declaration macro for enumeration type in global namespace
#define RTTI_DECLARE_ENUM( enumName )																\
	static_assert( std::is_enum< enumName >::value, "This macro should be used only for enum type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME( enumName );


//////////////////////////////////////////////////////////////////////////
// Declaration macro for enumeration type in named namespaces
#define RTTI_DECLARE_ENUM_IN_NAMESPACE( enumName, ... )												\
	static_assert( std::is_enum< BUILD_NAMESPACE( __VA_ARGS__ )::enumName >::value, "This macro should be used only for enum type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( enumName, __VA_ARGS__ );


//////////////////////////////////////////////////////////////////////////
// Definition macros for enumeration type in global namespace
#define RTTI_BEGIN_ENUM( name )			\
	RED_FORCE_LINK_THIS_FILE( name );	\
	_INTERNAL_RTTI_BEGIN_ENUM( name );


//////////////////////////////////////////////////////////////////////////
// Definition macro for enumeration type in named namespaces
#define RTTI_BEGIN_ENUM_IN_NAMESPACE( enumName, ... )							\
	RED_FORCE_LINK_THIS_FILE( JOIN_TOKENS_MACRO( enumName, __VA_ARGS__ ) );		\
	_INTERNAL_RTTI_BEGIN_ENUM_IN_NAMESPACE( enumName, BUILD_NAMESPACE( __VA_ARGS__ ), TO_STRING_MACRO( __VA_ARGS__, enumName ) );


//////////////////////////////////////////////////////////////////////////
// Definition macro for enumeration type script alias
#define RTTI_ENUM_SCRIPT_ALIAS( name ) RTTIRegisterScriptAlias( registeredEnum, name );


//////////////////////////////////////////////////////////////////////////
// Definition macro for options
#define RTTI_ENUM_OPTION( option ) rtti::EnumOptionBuilder( registeredEnum, RED_NAME_CONSTEXPR( #option ), Int64( enumType::option ) )

#define RTTI_ENUM_OPTION_ALIAS( aliasName, option ) rtti::EnumOptionBuilder( registeredEnum, RED_NAME_CONSTEXPR( aliasName ), Int64( enumType::option ) )


//////////////////////////////////////////////////////////////////////////
// End enumeration type definition
#define RTTI_END_ENUM() \
	RTTIRegisterType( registeredEnum, nativeHash ); } );



namespace rtti
{

	class EnumOptionBuilder
	{
	public:
		RED_INLINE EnumOptionBuilder()
			: m_enumType( nullptr )
		{}

		RED_INLINE EnumOptionBuilder( rtti::EnumType* enumType, const CName name, const Int64 value )
			: m_enumType( enumType )
			, m_value( value )
		{
			RED_ASSERT(m_enumType != nullptr, "Invalid EnumOptionBuilder usage");
			m_enumType->Add( name, value );
		}

		RED_INLINE EnumOptionBuilder& legacyName( const CName previousName )
		{
			RED_ASSERT(m_enumType != nullptr, "Invalid EnumOptionBuilder usage");
			m_enumType->AddLegacy( previousName, m_value );
			return *this;
		}

		RED_INLINE EnumOptionBuilder& legacyName( const red::StringView previousName )
		{
			return legacyName( RED_NAME( previousName ) );
		}

	private:
		rtti::EnumType* m_enumType;
		Int64 m_value;
	};

}
