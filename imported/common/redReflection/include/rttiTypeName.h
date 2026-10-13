/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "../../redCore/include/names.h"
#include "rttiInternalTypeName.h"

/// Pointer case
namespace rtti
{
	extern RED_REFLECTION_API const CName FormatPointerTypeName( const CName pointedTypeName );
	extern RED_REFLECTION_API const CName FormatNativeArrayTypeName( const CName innerTypeName, const Uint32 elementCount );
}

// partial specializations for pointers...
template< class _Type >
struct TTypeName< _Type* >
{
	static const CName GetTypeName()
	{
		static const CName name = rtti::FormatPointerTypeName( TTypeName<typename std::remove_cv< _Type >::type>::GetTypeName() );
		return name;
	}
};

// static array type name generator
template< class _Type, Uint32 _Count >
struct TTypeName< _Type[_Count] >
{
	static const CName GetTypeName()
	{
		static const CName name = rtti::FormatNativeArrayTypeName( TTypeName<_Type>::GetTypeName(), (Uint32) _Count );
		return name;
	}
};

template< class _Type >
RED_INLINE const CName GetTypeName()
{
	return TTypeName<_Type>::GetTypeName();
}

template< class _Type >
RED_INLINE const CName GetTypeName( const _Type& )
{
	return TTypeName<_Type>::GetTypeName();
}

/// Declare type _Type as recognizable by RTTI, must be in a visible place
#define RTTI_DECLARE_TYPE_NAME( _Type )															\
	static_assert( !std::is_enum< _Type >::value, "This macro cannot be used for enum type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME( _Type );

/// Declare type _Type as recognizable by RTTI, must be in a visible place - custom type name (advanced users)
#define RTTI_CUSTOM_TYPE_NAME( _Type, _CustomTypeName )											\
	static_assert( !std::is_enum< _Type >::value, "This macro cannot be used for enum type." );	\
	_INTERNAL_RTTI_DECLARE_CUSTOM_TYPE_NAME( _Type, _CustomTypeName )	\

/// Declare type _Type as recognizable by RTTI, must be in a visible place - version for types in namespaces
#define RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( typeName, ... )										\
	static_assert( !std::is_enum<  BUILD_NAMESPACE( __VA_ARGS__ )::typeName >::value, "This macro cannot be used for enum type." );	\
	_INTERNAL_RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( typeName, __VA_ARGS__ );

#define RTTI_CUSTOM_TYPE_NAME_IN_NAMESPACE( _Type, _CustomTypeName, ... )											\
	static_assert( !std::is_enum< BUILD_NAMESPACE( __VA_ARGS__ )::_Type >::value, "This macro cannot be used for enum type." );	\
	_INTERNAL_RTTI_DECLARE_CUSTOM_TYPE_NAME_IN_NAMESPACE( _Type, _CustomTypeName, __VA_ARGS__ )	\


