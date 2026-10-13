/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassBuilderUtils.h"
#include "rttiFunctionMacros.h"
#include "rttiRegistration.h"
#include "rttiClass.h"
#include "rttiNativeClass.h"

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_TYPE( typeName )																		\
	const rtti::ClassType* typeName::sm_classDesc = nullptr;														\
																													\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME( typeName );																	\
	_INTERNAL_RTTI_IMPLEMENT_TYPE( typeName );																		\
																													\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (															\
	[]()																											\
	{																												\
		TypeHash nativeHash = GetNativeTypeHash<typeName>();														\
																													\
		using Type = rtti::TNativeClass<typeName>;																	\
		using NoCopyType = rtti::TNativeClassNoCopy<typeName>;														\
		using FinalType = std::conditional< std::is_copy_assignable<typeName>::value &&								\
							std::is_copy_constructible<typeName>::value, Type, NoCopyType >::type;					\
		static FinalType registeredClass( RED_NAME_CONSTEXPR( #typeName ), sizeof( typeName ) );					\
																													\
		rtti::ClassDescSetter<typeName> assigner( &registeredClass );												\
		registeredClass.SetAlignment( __alignof( typeName ) );														\
		RTTIRegisterType( &registeredClass, nativeHash );															\
	},																												\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( typeName );


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_NODEFAULT_TYPE( typeName )																						\
	const rtti::ClassType* typeName::sm_classDesc = nullptr;																				\
																																			\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME( typeName );																							\
	_INTERNAL_RTTI_IMPLEMENT_TYPE( typeName );																								\
																																			\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (																					\
	[]()																																	\
	{																																		\
		TypeHash nativeHash = GetNativeTypeHash<typeName>();																				\
		static rtti::TNativeClass< typeName > registeredClass(																				\
			RED_NAME_CONSTEXPR( #typeName ), sizeof( typeName ), CF_NoDefaultObjectSerialization );											\
		rtti::ClassDescSetter<typeName> assigner( &registeredClass );																		\
		registeredClass.SetAlignment( __alignof( typeName ) );																				\
			RTTIRegisterType( &registeredClass, nativeHash );																				\
	},																																		\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( typeName );


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_ABSTRACT_TYPE( typeName )																			\
	const rtti::ClassType* typeName::sm_classDesc = nullptr;																	\
																																\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME( typeName );																				\
	_INTERNAL_RTTI_IMPLEMENT_TYPE( typeName );																					\
																																\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (																		\
	[]()																														\
	{																															\
		TypeHash nativeHash = GetNativeTypeHash<typeName>();																	\
		static rtti::TAbstractClassType< typeName > registeredClass(  RED_NAME_CONSTEXPR( #typeName ), sizeof( typeName ) );	\
		rtti::ClassDescSetter<typeName> assigner( &registeredClass );															\
		RTTIRegisterType( &registeredClass, nativeHash );																		\
	},																															\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( typeName );


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_TYPE_IN_NAMESPACE( typeName, namespace_, touchTypeToken_, namespacesString )									\
	const rtti::ClassType* namespace_::typeName::sm_classDesc = nullptr;																	\
																																			\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_WITH_NAMESPACE( typeName, namespace_, namespacesString );											\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_WITH_NAMESPACE( typeName, namespace_, touchTypeToken_ );													\
																																			\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (																					\
	[]()																																	\
	{																																		\
		TypeHash nativeHash = GetNativeTypeHash<namespace_::typeName>();																	\
																																			\
		using Type = rtti::TNativeClass<namespace_::typeName>;																				\
		using NoCopyType = rtti::TNativeClassNoCopy<namespace_::typeName>;																	\
		using FinalType = std::conditional< std::is_copy_assignable<namespace_::typeName>::value &&											\
							std::is_copy_constructible<namespace_::typeName>::value , Type, NoCopyType >::type;								\
		static FinalType registeredClass( RED_NAME_CONSTEXPR( namespacesString ), sizeof( namespace_::typeName ) );							\
																																			\
		rtti::ClassDescSetter<namespace_::typeName> assigner( &registeredClass );															\
		registeredClass.SetAlignment( __alignof( namespace_::typeName ) );																	\
		RTTIRegisterType( &registeredClass, nativeHash );																					\
	},																																		\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( namespace_::typeName );


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_NODEFAULT_TYPE_IN_NAMESPACE( typeName, namespace_, touchTypeToken_, namespacesString )								\
	const rtti::ClassType* namespace_::typeName::sm_classDesc = nullptr;																		\
																																				\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_WITH_NAMESPACE( typeName, namespace_, namespacesString );												\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_WITH_NAMESPACE( typeName, namespace_, touchTypeToken_ );														\
																																				\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (																						\
	[]()																																		\
	{																																			\
		TypeHash nativeHash = GetNativeTypeHash<namespace_::typeName>();																		\
																																				\
		static rtti::TNativeClass< namespace_::typeName > registeredClass(RED_NAME_CONSTEXPR(namespacesString), sizeof(namespace_::typeName),	\
																			CF_NoDefaultObjectSerialization);									\
																																				\
		rtti::ClassDescSetter<namespace_::typeName> assigner( &registeredClass );																\
		registeredClass.SetAlignment( __alignof( namespace_::typeName ) );																		\
		RTTIRegisterType( &registeredClass, nativeHash );																						\
	},																																			\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( namespace_::typeName );


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( typeName, namespace_, touchTypeToken_, namespacesString )			\
	const rtti::ClassType* namespace_::typeName::sm_classDesc = nullptr;													\
																															\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_WITH_NAMESPACE( typeName, namespace_, namespacesString );							\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_WITH_NAMESPACE( typeName, namespace_, touchTypeToken_ );									\
																															\
	static RTTIRegistrator RED_UNIQUE_NAME( Registrator ) (																	\
	[]()																													\
	{																														\
		TypeHash nativeHash = GetNativeTypeHash<namespace_::typeName>();													\
		static rtti::TAbstractClassType< namespace_::typeName > registeredClass(											\
			RED_NAME_CONSTEXPR( namespacesString ), sizeof( namespace_::typeName ) );										\
		rtti::ClassDescSetter<namespace_::typeName> assigner( &registeredClass );											\
		RTTIRegisterType( &registeredClass, nativeHash );																	\
	},																														\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( namespace_::typeName );

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_PROPERTY( field )																\
	rtti::PropertyBinder::AddPropertyToClass( registeredClass, currPropertyBuilder );					\
	currPropertyBuilder = RED_NEW( rtti::PropertyBuilder )												\
	(																									\
		registeredClass,																				\
		_INTERNAL_RTTI_OFFSETOF( tCurrClassType, field ),												\
		RED_NAME_CONSTEXPR( _INTERNAL_RTTI_PROPERTY_TXT( #field ) ),									\
		currCategoryMetadataBuilder.GetCategory(),														\
		::ResolveRttiType< RED_FIELD_DECLTYPE_NOCVREF( tCurrClassType, field ) >(),						\
		::rep::ReplicatedTypeResolver< RED_FIELD_DECLTYPE_NOCVREF( tCurrClassType, field ) >::GetType() \
	);																									\
	(*currPropertyBuilder)


//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_PROPERTY_OVERRIDE( field )														\
	rtti::PropertyBinder::AddPropertyOverrideToClass( registeredClass, currPropertyOverrideBuilder );	\
	currPropertyOverrideBuilder = RED_NEW( rtti::PropertyOverrideBuilder )								\
	(																									\
		registeredClass,																				\
		RED_NAME_CONSTEXPR( _INTERNAL_RTTI_PROPERTY_TXT( #field ) )										\
	);																									\
	(*currPropertyOverrideBuilder)

//////////////////////////////////////////////////////////////////////////
#define _INTERNAL_RTTI_PROPERTY_BITFIELD( field_, type_ )								\
	rtti::PropertyBinder::AddPropertyToClass( registeredClass, currPropertyBuilder );	\
	currPropertyBuilder = RED_NEW( rtti::PropertyBuilder )								\
	(																					\
	registeredClass,																	\
	_INTERNAL_RTTI_OFFSETOF( tCurrClassType, field_ ),									\
	RED_NAME_CONSTEXPR( _INTERNAL_RTTI_PROPERTY_TXT( #field_ ) ),						\
	currCategoryMetadataBuilder.GetCategory(),											\
	::GetTypeObject< type_ >()															\
	);																					\
	(*currPropertyBuilder)

#define _INTERNAL_RTTI_DISALLOW_TYPE_IN_NAMESPACE( typeName, touchTypeToken_ ) \
	const rtti::ClassType* touchTypeToken_() { return nullptr; }