/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace rtti
{
	class IType;

	//! Check if we can cast from one type to another ( no conversion )
	extern RED_REFLECTION_API const Bool CanCast( const CName sourceType, const CName destType );

	//! Check if we can cast from one type to another ( no conversion )
	extern RED_REFLECTION_API const Bool CanCast( const rtti::IType* sourceType, const rtti::IType* destType );

	//! Create a type name for a custom dynamic array
	extern RED_REFLECTION_API const CName FormatDynArrayTypeName( const CName innerTypeName );

	//! Create a type name for a custom native array
	extern RED_REFLECTION_API const CName FormatNativeArrayTypeName( const CName innerTypeName, const Uint32 elementCount );

	//! Create a type name for a custom handle type
	extern RED_REFLECTION_API const CName FormatHandleTypeName( const CName pointedTypeName );

	//! Create a type name for a custom weak handle type
	extern RED_REFLECTION_API const CName FormatWeakHandleTypeName( const CName pointedTypeName );

	//! Create a type name for a custom static array
	extern RED_REFLECTION_API const CName FormatStaticArrayTypeName( const CName innerTypeName, const Uint32 maxSize );

	//! Create a type name for a scripted reference
	extern RED_REFLECTION_API const CName FormatScriptedReferenceTypeName( const CName pointerTypeName );

	//! Create a type name for a custom pointer
	extern RED_REFLECTION_API const CName FormatPointerTypeName( const CName pointedTypeName );

	//! Create a type name for a resource reference
	extern RED_REFLECTION_API const CName FormatResRefTypeName( const CName pointedTypeName );

	//! Create a type name for a resource async reference
	extern RED_REFLECTION_API const CName FormatResAsyncRefTypeName( const CName pointedTypeName );

	extern RED_REFLECTION_API const CName GetFilteredPropertyName( CName propName );

} // rtti
