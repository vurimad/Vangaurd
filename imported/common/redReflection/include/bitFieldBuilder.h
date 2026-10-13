/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "bitFieldInternalBuilder.h"
#include "rttiInternalTypeName.h"

//////////////////////////////////////////////////////////////////////////
// Declaration macro for bit field in global namespace
#define RTTI_DECLARE_BITFIELD( bitFieldName )																					\
	static_assert( std::is_enum< bitFieldName >::value, "This macro should be used only for enum type as bit field type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME( bitFieldName );


//////////////////////////////////////////////////////////////////////////
// Declaration macro for bit field in named namespaces
#define RTTI_DECLARE_BITFIELD_IN_NAMESPACE( bitFieldName, ... )															\
	static_assert( std::is_enum< BUILD_NAMESPACE( __VA_ARGS__ )::bitFieldName >::value, "This macro should be used only for enum type as bit field type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( bitFieldName, __VA_ARGS__ );


//////////////////////////////////////////////////////////////////////////
// Definition macros for bit field in global namespace
#define RTTI_BEGIN_BITFIELD( bitFieldName, size )			\
	RED_FORCE_LINK_THIS_FILE( bitFieldName );				\
	_INTERNAL_RTTI_BEGIN_BITFIELD( bitFieldName, size );


//////////////////////////////////////////////////////////////////////////
// Definition macro for bit field in named namespaces
#define RTTI_BEGIN_BITFIELD_IN_NAMESPACE( bitFieldName, ... ) \
	RED_FORCE_LINK_THIS_FILE( JOIN_TOKENS_MACRO( bitFieldName, __VA_ARGS__ ) );		\
	_INTERNAL_RTTI_BEGIN_BITFIELD_IN_NAMESPACE( bitFieldName, BUILD_NAMESPACE( __VA_ARGS__ ), TO_STRING_MACRO( __VA_ARGS__, bitFieldName ) );


//////////////////////////////////////////////////////////////////////////
// Definition macro for options
#define RTTI_BITFIELD_OPTION( option ) \
	registeredBitField->AddBit( RED_NAME_CONSTEXPR( #option ), static_cast< Uint64 >( bitFieldType::option ) );

#define RTTI_BITFIELD_OPTION_ALIAS( aliasName, option ) \
	registeredBitField->AddBit( RED_NAME_CONSTEXPR( aliasName ), static_cast< Uint64 >( bitFieldType::option ) );


//////////////////////////////////////////////////////////////////////////
// End bit field definition
#define RTTI_END_BITFIELD() \
	GetRttiSystem().RegisterType( registeredBitField, nativeHash ); } ); 
