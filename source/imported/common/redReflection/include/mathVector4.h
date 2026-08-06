/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"

#include "../../redMath/include/vector3.h"
#include "../../redMath/include/vector2.h"
#include "../../redFileSystem/include/file.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

class IScriptable;
class CScriptStackFrame;

/// 4-element vector, RTTI wrapped
RED_ALIGNED_STRUCT_API( Vector4, RED_REFLECTION_API, 16 ) : public math::Vector4
{
	RTTI_DECLARE_TYPE( Vector4 );

	RED_FORCE_INLINE Vector4() = default;

	RED_FORCE_INLINE Vector4( const Vector4& v )
		: math::Vector4{ v }
	{}

	RED_FORCE_INLINE Vector4( const math::Vector4& v )
		: math::Vector4{ v }
	{}

	RED_FORCE_INLINE Vector4( const math::Vector3& v, const Float w = 1.0f )
		: math::Vector4{ v.X, v.Y, v.Z, w }
	{}

	RED_FORCE_INLINE Vector4( const math::Vector2& v )
		: math::Vector4{ v }
	{}

	RED_FORCE_INLINE Vector4( const Float f[4] )
		: math::Vector4{ f }
	{}

	Vector4( std::nullptr_t ) = delete;

	RED_FORCE_INLINE Vector4( Float x, Float y, Float z, Float w=1.0f )
		: math::Vector4{ x, y, z, w }
	{}

	RED_FORCE_INLINE Vector4& operator=( const Vector4& v )
	{
		math::Vector4::operator=(v);
		return *this;
	}

	RED_FORCE_INLINE static Vector4 ZEROS()
	{
		return math::Vector4::ZEROS();
	}

	RED_FORCE_INLINE static Vector4 Lerp( const Vector4& a, const Vector4& b, float t )
	{
		return Vector4( math::Lerp( t, a.X, b.X ), math::Lerp( t, a.Y, b.Y ), math::Lerp( t, a.Z, b.Z ), math::Lerp( t, a.W, b.W ) );
	}

	static void funcDot2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcCross( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcLength2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcLength( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcLengthSquared( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcNormalize2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcNormalize( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRand2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRand( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcMirror( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDistance( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDistanceSquared( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDistance2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDistanceSquared2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcDistanceToEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcNearestPointOnEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcHeading( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcFromHeading( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransform( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRotateAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformDir( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformH( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAngleBetween( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAngleDegAroundAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcProjectPointToPlane( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
};

RED_ALLOW_TYPE_AS_POD( Vector4 );

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Vector4>( const math::Vector4& )
{
	return TTypeName<Vector4>::GetTypeName();
}

// Manual serialization
RED_INLINE void operator<<( IFile& file, math::Vector4& v )
{
	static_assert( sizeof( v ) == 16, "" );
	file.Serialize( &v, sizeof( v ) );
}

RED_INLINE void operator<<( IFile& file, simd::Vector4& v )
{
	static_assert( sizeof( v ) == 16, "" );
	file.Serialize( &v, sizeof( v ) );
}

// Manual serialization
RED_INLINE void operator<<( IFile& file, Vector4& val )
{
	static_assert( sizeof( val ) == 16, "" );
	file.Serialize( &val, sizeof( val ) );
}

// allow simplified copying of the type
template <> struct TCopyableType<math::Vector4>	{ enum { Value = true }; };
template <> struct TCopyableType<Vector4>		{ enum { Value = true }; };

RED_REFLECTION_API Bool ToString( red::String& outTxt, const Vector4& val );

RED_REFLECTION_API Bool ToStringMaxPrecision( red::String& outTxt, const Vector4& val );

namespace red
{
	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<Vector4, Length>
	{
		static_assert(Length == 0, "Length is inapplicable");
		using StorageType = Vector4;
		using SetType = StorageType;

		static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			// #tbd: ToBufferMaxPrecision()?
			const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen, "[%g, %g, %g, %g]", val.X, val.Y, val.Z, val.W);
			return ret > -1;
		}
	};
}