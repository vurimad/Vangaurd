/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "redReflectionApi.h"
#include <typeinfo>

namespace rtti
{
	class IType;
}

// Native function handler for ISerializable's
typedef void ( *TNativeGlobalFunc )( class IScriptable* context, class CScriptStackFrame&, void*, const rtti::IType* );
typedef void ( IScriptable::*TNativeFunc )( class CScriptStackFrame&, void*, const rtti::IType* );

/// Meta type (type of type)
// ctremblay: DO NOT CHANGE ORDER / VALUE or add entry to this enum. Serialization code rely on this. 
enum ERTTITypeType : Uint8
{	
	RT_Name,					// CName
	RT_Fundamental,				// POD type
	RT_Class,					// rtti::Class
	RT_Array,					// red::DynArray
	RT_Simple,					// non POD type
	RT_Enum,					// rtti::Enum
	RT_StaticArray,				// red::StaticArray
	RT_NativeArray,				// T[N]
	RT_Pointer,					// rtti::IBasePointerType -> rtti::PointerType
	RT_Handle,					// rtti::IBasePointerType -> rtti::HandleType
	RT_WeakHandle,				// rtti::IBasePointerType -> rtti::WeakHandleType
	RT_ResourceReference,		// ResourceReference
	RT_ResourceAsyncReference,	// ResourceAsyncReference
	RT_BitField,				// rtti::BitField
	RT_LegacySingleChannelCurve, // ctrmblay: HACK remove this.
	RT_ScriptReference,			// rtti::ScriptedReferenceType
	RT_Count,
};

typedef Uint64 TypeHash;

namespace rtti
{
	extern RED_REFLECTION_API Uint32 GenerateUniqueTypeHash();
}

#ifdef RED_HAS_NATIVE_RTTI

	/// This is hack for Clang compiler because we cannot use typeid on only forward declared type
	/// but we can specialize template by only forward declared type and use typeid on that new created type
	template< typename T >
	class ClassWrapper
	{
	};

	/// Returns unique hash code for given type, uses hack prepared for Clang compiler ( described above )
	template< typename T >
	TypeHash GetNativeTypeHash()
	{
		return typeid( ClassWrapper< T > ).hash_code();
	}

#elif !defined( RED_DLL ) && !defined( RED_WITH_DLL )

	/// Returns unique hash code for given type using custom hash generation function
	template< typename T >
	TypeHash GetNativeTypeHash()
	{
		static Uint32 nativeTypeHash = rtti::GenerateUniqueTypeHash();
		return nativeTypeHash;
	}

#else

	#error "GetNativeTypeHash() implementation does not work with DLL configurations without native C++ RTTI on"

#endif // RED_HAS_NATIVE_RTTI

/// Operation context for RTTI operations, can be used to report extended errors
class RED_REFLECTION_API IRTTIContext : red::NonCopyable
{
public:
	virtual ~IRTTIContext();
	virtual const Bool ReportError( const class rtti::IType* typeInfo, STATIC_CHECK_PRINTF_MSC const char* txt, ... ) = 0;

	static IRTTIContext& GetDefault();
	static IRTTIContext& GetDummy();
};
