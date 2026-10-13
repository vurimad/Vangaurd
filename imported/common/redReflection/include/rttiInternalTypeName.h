/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redCore/include/names.h"

// TTypeName templates for providing human-readable names for RTTI types
template< class _Type >
struct TTypeName
{
	// By default try to get the type name from the type itself
	static const CName GetTypeName()
	{
		return _Type::GetTypeName();
	}
};

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_DECLARE_TYPE_NAME( _Type )						\
template<> struct TTypeName<_Type>										\
{																		\
	static const CName GetTypeName()									\
	{																	\
		static CName theName = RED_NAME_CONSTEXPR( #_Type );				\
		return theName;													\
	}																	\
}

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_DECLARE_CUSTOM_TYPE_NAME( _Type, _CustomTypeName )	\
template<> struct TTypeName<_Type>											\
{																			\
	static const CName GetTypeName()										\
	{																		\
		static CName theName = RED_NAME_CONSTEXPR( _CustomTypeName );			\
		return theName;														\
	}																		\
}

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( typeName, ... )							\
template<> struct TTypeName< BUILD_NAMESPACE( __VA_ARGS__ )::typeName >							\
{																								\
	static const CName GetTypeName()															\
	{																							\
		static CName theName = RED_NAME_CONSTEXPR( TO_STRING_MACRO( __VA_ARGS__, typeName ) );	\
		return theName;																			\
	}																							\
}

#define _INTERNAL_RTTI_DECLARE_CUSTOM_TYPE_NAME_IN_NAMESPACE( _Type, _CustomTypeName, ... )	\
template<> struct TTypeName< BUILD_NAMESPACE( __VA_ARGS__ )::_Type >						\
{																							\
	static const CName GetTypeName()														\
	{																						\
		static CName theName = RED_NAME_CONSTEXPR( _CustomTypeName );							\
		return theName;																		\
	}																						\
}
