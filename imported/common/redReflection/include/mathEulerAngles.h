/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "../../redMath/include/eulerAngles.h"
#include "mathVector3.h"
#include "rttiClassDeclarationMacros.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

/********************************************************/
/* Euler angles, rotations are CCW, order: Y X Z		*/
/********************************************************/
struct RED_REFLECTION_API EulerAngles : public math::EulerAngles
{
	RTTI_DECLARE_TYPE( EulerAngles );

	RED_FORCE_INLINE EulerAngles() = default;

	RED_FORCE_INLINE EulerAngles( const math::EulerAngles &ea )
		: math::EulerAngles{ ea }
	{}

	RED_FORCE_INLINE EulerAngles( Float roll, Float pitch, Float yaw )
		: math::EulerAngles{ roll, pitch, yaw }
	{}

	RED_FORCE_INLINE EulerAngles( const Vector3& v )
		: math::EulerAngles( v )
	{}

	RED_FORCE_INLINE EulerAngles( const Float f[ 3 ] )
		: math::EulerAngles{ f }
	{}

	static void funcGetXAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetYAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetZAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetForward( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRight( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetUp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToMatrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToQuat(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxes( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRand( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcAlmostEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
};


// allow simplified copying of the type
template <> struct TCopyableType<math::EulerAngles>	{ enum { Value = true }; };
template <> struct TCopyableType<EulerAngles>		{ enum { Value = true }; };

// Type aliasing for serialization
RED_INLINE const CName GetTypeName( const math::EulerAngles& )
{
	return TTypeName<EulerAngles>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, EulerAngles& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::EulerAngles& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}

RED_FORCE_INLINE const Bool ToString( red::String& outTxt, const EulerAngles& val )
{
	red::String yawAsString = String_CreateExternal_OnStack( 128 );
	red::String pitchAsString = String_CreateExternal_OnStack( 128 );
	red::String rollAsString = String_CreateExternal_OnStack( 128 );

	if( ::ToString( yawAsString, val.Yaw ) &&
		::ToString( pitchAsString, val.Pitch ) &&
		::ToString( rollAsString, val.Roll ) )
	{
		outTxt = "[";
		outTxt += yawAsString;
		outTxt += " ";
		outTxt += pitchAsString;
		outTxt += " ";
		outTxt += rollAsString;
		outTxt += "]";
		return true;
	}
	return false;
}
