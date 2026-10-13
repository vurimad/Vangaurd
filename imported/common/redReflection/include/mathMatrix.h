/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

/// 4x4 matrix
struct RED_REFLECTION_API Matrix : public math::Matrix
{
	RTTI_DECLARE_TYPE( Matrix );

	RED_FORCE_INLINE Matrix() = default;

	RED_FORCE_INLINE Matrix( const Float f[16] )
		: math::Matrix( f )
	{}

	RED_FORCE_INLINE Matrix( const math::Matrix& a )
		: math::Matrix( a )
	{}

	RED_FORCE_INLINE Matrix( const math::Vector4& x, const math::Vector4& y, const math::Vector4& z, const math::Vector4& w )
		: math::Matrix( x,y,z,w )
	{}

	RED_FORCE_INLINE Matrix( const Float _00, const Float _01, const Float _02, const Float _03,
					  const Float _10, const Float _11, const Float _12, const Float _13,
					  const Float _20, const Float _21, const Float _22, const Float _23,
					  const Float _30, const Float _31, const Float _32, const Float _33 )
		: math::Matrix{ _00, _01, _02, _03, _10, _11, _12, _13, _20, _21, _22, _23,	_30, _31, _32, _33 }
	{}

	RED_FORCE_INLINE Matrix& operator=( const Matrix& m )
	{
		math::Matrix::operator=(m);
		return *this;
	}

	static const Matrix IDENTITY_CONSTANT;

	RED_FORCE_INLINE static Matrix IDENTITY()
	{
		return math::Matrix::IDENTITY();
	}

	RED_FORCE_INLINE static Matrix ZEROS()
	{
		return math::Matrix::ZEROS();
	}

	// Script functions
	static void funcIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltTranslation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltPreScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltTRS( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuiltRTS( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcBuildFromDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetTranslation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxisX( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxisY( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetAxisZ( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetInverted( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetInvertedFull( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToQuat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcIsOk( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
};

RED_ALLOW_TYPE_AS_POD( Matrix );


// allow simplified copying of the type
template <> struct TCopyableType<math::Matrix>	{ enum { Value = true }; };
template <> struct TCopyableType<Matrix>		{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Matrix>( const math::Matrix& )
{
	return TTypeName<Matrix>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Matrix& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Matrix& val )
{
	static_assert( sizeof( val ) == 64, "" );
	file.Serialize( &val, sizeof( val ) );
}
