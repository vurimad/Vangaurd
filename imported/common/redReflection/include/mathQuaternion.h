/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiInternalTypeName.h"
#include "scriptable.h"

#include "../../redFileSystem/include/file.h"
#include "../../../common/redMath/include/quaternion.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

/// 4-element quaternion, RTTI wrapped
RED_ALIGNED_STRUCT_API( Quaternion, RED_REFLECTION_API, 16 ) : public math::Quaternion
{
	RTTI_DECLARE_TYPE( Quaternion );

	RED_FORCE_INLINE Quaternion() = default;

	RED_FORCE_INLINE Quaternion( const math::Quaternion& q )
		: math::Quaternion{ q }
	{}

	RED_FORCE_INLINE Quaternion( const Quaternion& q )
		: math::Quaternion{ q }
	{}

	RED_FORCE_INLINE Quaternion( const Float i, const Float j, const Float k, const Float r )
		: math::Quaternion{ i, j, k, r }
	{}

	RED_FORCE_INLINE Quaternion( const Float arr[4] )
		: math::Quaternion{ arr }
	{}

	RED_FORCE_INLINE Quaternion( const math::Vector4& axis, const Float angle )
		: math::Quaternion{ axis, angle }
	{}

	RED_FORCE_INLINE Quaternion( const math::Vector3& axis, const Float angle )
		: math::Quaternion{ axis, angle }
	{}

	RED_FORCE_INLINE Quaternion& operator=( const math::Quaternion& q )
	{
		math::Quaternion::operator=(q);
		return *this;
	}

	static void funcGetXAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetYAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetZAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetForward(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRight(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetUp(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToMatrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToEulerAngles( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxes(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDot(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRand(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransform( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetShortestRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetAxisAngle( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetXRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetYRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetZRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcLerp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSlerp( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAngle( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcNormalize(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcNormalized(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcMagnitude(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcMagnitudeSq(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcMulInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcConjugate( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuildFromDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
};

RED_ALLOW_TYPE_AS_POD( Quaternion );

// allow simplified copying of the type
template <> struct TCopyableType<math::Quaternion>	{ enum { Value = true }; };
template <> struct TCopyableType<Quaternion>		{ enum { Value = true }; };

// Type aliasing for serialization
RED_INLINE const CName GetTypeName( const math::Quaternion& )
{
	return TTypeName<Quaternion>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Quaternion& q )
{
	// TODO bulk serialisation
	// FIXME this is serialised differently than memory layout
	//static_assert( sizeof( q ) == 16, "" );
	file << q.r;
	file << q.i;
	file << q.j;
	file << q.k;
	//file.Serialize( &val, sizeof( val ) );
}

RED_FORCE_INLINE void operator<<( IFile& file, math::Quaternion& q )
{
	// TODO bulk serialisation
	// FIXME this is serialised differently than memory layout
	//static_assert( sizeof( q ) == 16, "" );
	file << q.r;
	file << q.i;
	file << q.j;
	file << q.k;
	//file.Serialize( &val, sizeof( val ) );
}

RED_FORCE_INLINE const Bool ToString( red::String& outTxt, const Quaternion& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );
	red::String wAsString = String_CreateExternal_OnStack( 128 );
	
	if ( ::ToString( xAsString, val.i ) &&
		 ::ToString( yAsString, val.j ) &&
		 ::ToString( zAsString, val.k ) &&
		 ::ToString( wAsString, val.r ) )
	{
		red::String quatAsString = String_CreateExternal_OnStack( 512 );
		quatAsString += "[";
		quatAsString += xAsString;
		quatAsString += " ";
		quatAsString += yAsString;
		quatAsString += " ";
		quatAsString += zAsString;
		quatAsString += " ";
		quatAsString += wAsString;
		quatAsString += "]";
		outTxt = quatAsString;
		return true;
	}
	return false;
}

RED_FORCE_INLINE const Bool ToStringMaxPrecision( red::String& outTxt, const Quaternion& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );
	red::String wAsString = String_CreateExternal_OnStack( 128 );
	
	if ( ::ToStringMaxPrecision( xAsString, val.i ) &&
		 ::ToStringMaxPrecision( yAsString, val.j ) &&
		 ::ToStringMaxPrecision( zAsString, val.k ) &&
		 ::ToStringMaxPrecision( wAsString, val.r ) )
	{
		red::String quatAsString = String_CreateExternal_OnStack( 512 );
		quatAsString += "[";
		quatAsString += xAsString;
		quatAsString += " ";
		quatAsString += yAsString;
		quatAsString += " ";
		quatAsString += zAsString;
		quatAsString += " ";
		quatAsString += wAsString;
		quatAsString += "]";
		outTxt = quatAsString;
		return true;
	}
	return false;
}

namespace red
{
	template < Uint32 Length >
	struct err::CrashDataTypeAdapter< Quaternion, Length >
	{
		static_assert( Length == 0, "Length is inapplicable" );
		using StorageType = Quaternion;
		using SetType = StorageType;

		static CrashDataCopyResult Copy( StorageType& mem, const SetType& value )
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print( char* buffer, Uint32 bufferLen, const StorageType& val )
		{
			// #tbd: ToBufferMaxPrecision()?
			const Int32 ret = red::SNPrintFUnsafe( buffer, bufferLen, "[%g, %g, %g, %g]", val.i, val.j, val.k, val.r );
			return ret > -1;
		}
	};
}
