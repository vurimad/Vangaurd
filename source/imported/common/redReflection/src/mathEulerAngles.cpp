/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathEulerAngles.h"
#include "mathQuaternion.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "rttiClassBuilder.h"
#include "../../redMath/include/random.h"

RTTI_BEGIN_NODEFAULT_TYPE( EulerAngles );
	RTTI_PROPERTY( Pitch ).setName( "Pitch" ).editable().replicated();
	RTTI_PROPERTY( Yaw ).setName( "Yaw" ).editable().replicated();
	RTTI_PROPERTY( Roll ).setName( "Roll" ).editable().replicated();

	// EulerAngles natives
	RTTI_NATIVE_STATIC_FUNCTION( "GetXAxis", funcGetXAxis );
	RTTI_NATIVE_STATIC_FUNCTION( "GetYAxis", funcGetYAxis );
	RTTI_NATIVE_STATIC_FUNCTION( "GetZAxis", funcGetZAxis );
	RTTI_NATIVE_STATIC_FUNCTION( "GetForward", funcGetForward );
	RTTI_NATIVE_STATIC_FUNCTION( "GetRight", funcGetRight );
	RTTI_NATIVE_STATIC_FUNCTION( "GetUp", funcGetUp );
	RTTI_NATIVE_STATIC_FUNCTION( "ToMatrix", funcToMatrix );
	RTTI_NATIVE_STATIC_FUNCTION( "ToQuat", funcToQuat);
	RTTI_NATIVE_STATIC_FUNCTION( "GetAxes", funcGetAxes );
	RTTI_NATIVE_STATIC_FUNCTION( "Rand", funcRand );
	RTTI_NATIVE_STATIC_FUNCTION( "Dot", funcDot );
	RTTI_NATIVE_STATIC_FUNCTION( "AlmostEqual", funcAlmostEqual );
RTTI_END_TYPE();

void EulerAngles::funcGetXAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( NULL, &ret, NULL );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcGetYAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( &ret, NULL, NULL );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcGetZAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( NULL, NULL, &ret );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcGetForward( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( &ret, NULL, NULL );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcGetRight( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( NULL, &ret, NULL );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcGetUp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 ret;
	rot.ToAngleVectors( NULL, NULL, &ret );
	RETURN_STRUCT( Vector4, ret );
}

void EulerAngles::funcToMatrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, rot.ToMatrix() );
}

void EulerAngles::funcToQuat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(EulerAngles, rot, EulerAngles::ZEROS());
	FINISH_PARAMETERS;

	auto quat = rot.ToQuat();
	RETURN_STRUCT(Quaternion, quat);
}

void EulerAngles::funcGetAxes( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	GET_PARAMETER_REF( Vector4, forward, Vector4::ZEROS() );
	GET_PARAMETER_REF( Vector4, right, Vector4::ZEROS() );
	GET_PARAMETER_REF( Vector4, up, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	rot.ToAngleVectors( &forward, &right, &up );

	RETURN_VOID();
}

void EulerAngles::funcDot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, a, EulerAngles::ZEROS() );
	GET_PARAMETER_OPT( EulerAngles, b, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 forwardA = Vector4::ZEROS();
	a.ToAngleVectors( &forwardA, NULL, NULL );

	Vector4 forwardB = Vector4::ZEROS();
	b.ToAngleVectors( &forwardB, NULL, NULL );

	RETURN_FLOAT( Vector4::Dot3( forwardA, forwardB ) );
}

void EulerAngles::funcRand( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, rotMin, 0.0f );
	GET_PARAMETER( Float, rotMax, 0.0f );
	FINISH_PARAMETERS;

	EulerAngles rot	= EulerAngles::ZEROS();
	rot.Pitch		= math::DefaultRandom().Get<Float>( rotMin , rotMax );
	rot.Yaw			= math::DefaultRandom().Get<Float>( rotMin , rotMax );

	RETURN_STRUCT( EulerAngles, rot );
}

void EulerAngles::funcAlmostEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EulerAngles, a, EulerAngles::ZEROS() );
	GET_PARAMETER( EulerAngles, b, EulerAngles::ZEROS() );
	GET_PARAMETER( math::Float, epsilon, 0.1f );
	FINISH_PARAMETERS;

	RETURN_BOOL( a.AlmostEquals( b, epsilon ) );
}