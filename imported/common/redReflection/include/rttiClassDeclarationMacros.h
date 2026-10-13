#pragma once

#include "serializationUtils.h"

namespace rtti
{
	class ClassType;

	template< typename T >
	class ClassDescSetter;

	template< typename T >
	class TNativeClass;

	template< typename T >
	class TNativeClassNoCopy;
}

#ifdef RED_CONFIGURATION_FINAL
#	define _INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION
#	define _INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION_ABSTRACT
#else
#	define _INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION const AnsiChar* GetNativeTypeName() const;
#	define _INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION_ABSTRACT virtual const AnsiChar* GetNativeTypeName() const;
#endif

//////////////////////////////////////////////////////////////////////////
// Use this macro for NON polymorphic types or for types which are NOT first class in inheritance hierarchy
#define RTTI_DECLARE_TYPE( typeName )													\
private:																				\
	static const rtti::ClassType* sm_classDesc;											\
public:																					\
	friend rtti::TNativeClass< typeName >;												\
	friend rtti::TNativeClassNoCopy< typeName >;										\
	friend class rtti::ClassDescSetter< typeName >;										\
	const rtti::ClassType* GetNativeClass() const;										\
	const rtti::ClassType* GetClass() const;											\
	_INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION										\
	static const rtti::ClassType* GetStaticClass();										\
	static void RegisterProperties( rtti::ClassType* registeredClass );					\
	static CName GetTypeName();


//////////////////////////////////////////////////////////////////////////
// Use this macro ONLY for first class in inheritance hierarchy
#define RTTI_DECLARE_POLYMORPHIC_TYPE( typeName )											\
private:																					\
	static const rtti::ClassType* sm_classDesc;												\
public:																						\
	friend rtti::TNativeClass< typeName >;													\
	friend rtti::TNativeClassNoCopy< typeName >;											\
	friend class rtti::ClassDescSetter< typeName >;											\
	virtual const rtti::ClassType* GetNativeClass() const;									\
	virtual const rtti::ClassType* GetClass() const;										\
	_INTERNAL_RTTI_GET_NATIVE_TYPENAME_DECLARATION_ABSTRACT									\
	static const rtti::ClassType* GetStaticClass();											\
	static void RegisterProperties( rtti::ClassType* registeredClass );						\
	static CName GetTypeName();
