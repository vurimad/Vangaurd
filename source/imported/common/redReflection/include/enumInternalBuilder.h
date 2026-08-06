/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiEnum.h"

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_ENUM( enumName )													\
static RTTIRegistrator RED_UNIQUE_NAME( Registrator )( []()										\
{																								\
	static_assert( std::is_signed< std::underlying_type_t< enumName > >::value || std::numeric_limits< std::underlying_type_t< enumName > >::max() < std::numeric_limits< Int64 >::max(), "RTTI enum underlying type must be convertible to Int64" ); \
	TypeHash nativeHash = GetNativeTypeHash<enumName>();									\
	auto registeredEnum = RED_NEW( rtti::EnumType )( RED_NAME_CONSTEXPR( #enumName ), sizeof( enumName ), false );	\
	using enumType = enumName;

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_ENUM_IN_NAMESPACE( enumName, namespace_, namespaceEnumString_ )							\
static RTTIRegistrator RED_UNIQUE_NAME( Registrator )( []()																\
{																														\
	static_assert( std::is_signed< std::underlying_type_t< namespace_::enumName > >::value || std::numeric_limits< std::underlying_type_t< namespace_::enumName > >::max() < std::numeric_limits< Int64 >::max(), "RTTI enum underlying type must be convertible to Int64" ); \
	TypeHash nativeHash = GetNativeTypeHash<namespace_::enumName>();													\
	auto registeredEnum = RED_NEW( rtti::EnumType )( RED_NAME_CONSTEXPR( namespaceEnumString_ ), sizeof( namespace_::enumName ), false );	\
	using enumType = namespace_::enumName;
