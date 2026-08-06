/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiBitField.h"

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_BITFIELD( bitFieldName, size )										\
static RTTIRegistrator RED_UNIQUE_NAME( Registrator )( []()											\
{																								\
	TypeHash nativeHash = GetNativeTypeHash<bitFieldName>();									\
	auto registeredBitField = RED_NEW( rtti::BitFieldType )( RED_NAME_CONSTEXPR( #bitFieldName ), size, false );	\
	using bitFieldType = bitFieldName;

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_BITFIELD_IN_NAMESPACE( bitFieldName, namespace_, namespaceBitFieldString_ )								\
static RTTIRegistrator RED_UNIQUE_NAME( Registrator )( []()																					\
{																																		\
	TypeHash nativeHash = GetNativeTypeHash<namespace_::bitFieldName>();																\
	auto registeredBitField = RED_NEW( rtti::BitFieldType )( RED_NAME_CONSTEXPR( namespaceBitFieldString_ ), sizeof( namespace_::bitFieldName ), false );	\
	using bitFieldType = namespace_::bitFieldName;
