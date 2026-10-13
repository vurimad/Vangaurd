/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "engineTime.h"

#include "../../../common/redReflection/include/scriptStackFrame.h"
#include "../../redContainers/include/fundamentalStringConversion.h"

void EngineTime::funcIsValid( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a.IsValid() );
}

void EngineTime::funcFromFloat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, EngineTime( a ) );
}

void EngineTime::funcToFloat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_FLOAT( Float( a ) );
}

void EngineTime::funcToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_STRING( ToStringDirect( Double( a ) ) );
}

void funcOperatorAdd_EngineTimeEngineTime_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a + b );	
}

void funcOperatorAdd_EngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a + b );	
}

void funcOperatorAssignAdd_OutEngineTimeEngineTime_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a += b );	
}

void funcOperatorAssignAdd_OutEngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a += b );	
}

void funcOperatorSubtract_EngineTimeEngineTime_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a - b );	
}

void funcOperatorSubtract_EngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a - b );	
}

void funcOperatorAssignSubtract_OutEngineTimeEngineTime_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a -= b );	
}

void funcOperatorAssignSubtract_OutEngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a -= b );	
}

void funcOperatorMultiply_EngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a * b );	
}

void funcOperatorAssignMultiply_OutEngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, a *= b );	
}

void funcOperatorDivide_EngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, b ? (a / b) : EngineTime::ZERO );	
}

void funcOperatorAssignDivide_OutEngineTimeFloat_EngineTime( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( EngineTime, b ? (a /= b) : (a = EngineTime::ZERO) );	
}

void funcOperatorEqual_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a == b );
}

void funcOperatorNotEqual_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a != b );
}

void funcOperatorGreater_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a > b );
}

void funcOperatorGreaterEqual_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a >= b );
}

void funcOperatorLess_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a < b );
}

void funcOperatorLessEqual_EngineTimeEngineTime_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( EngineTime, b, EngineTime::ZERO );
	FINISH_PARAMETERS;
	RETURN_BOOL( a <= b );
}

void funcOperatorGreater_EngineTimeFloat_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_BOOL( a > b );
}

void funcOperatorGreaterEqual_EngineTimeFloat_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_BOOL( a >= b );
}

void funcOperatorLess_EngineTimeFloat_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_BOOL( a < b );
}

void funcOperatorLessEqual_EngineTimeFloat_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( EngineTime, a, EngineTime::ZERO );
	GET_PARAMETER( Float, b, 0.f );
	FINISH_PARAMETERS;
	RETURN_BOOL( a <= b );
}

void RegisterScriptEngineTimeNatives()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;EngineTimeEngineTime;EngineTime", funcOperatorAdd_EngineTimeEngineTime_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;EngineTimeFloat;EngineTime", funcOperatorAdd_EngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutEngineTimeEngineTime;EngineTime", funcOperatorAssignAdd_OutEngineTimeEngineTime_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutEngineTimeFloat;EngineTime", funcOperatorAssignAdd_OutEngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;EngineTimeEngineTime;EngineTime", funcOperatorSubtract_EngineTimeEngineTime_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;EngineTimeFloat;EngineTime", funcOperatorSubtract_EngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutEngineTimeEngineTime;EngineTime", funcOperatorAssignSubtract_OutEngineTimeEngineTime_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutEngineTimeFloat;EngineTime", funcOperatorAssignSubtract_OutEngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;EngineTimeFloat;EngineTime", funcOperatorMultiply_EngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutEngineTimeFloat;EngineTime", funcOperatorAssignMultiply_OutEngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorDivide;EngineTimeFloat;EngineTime", funcOperatorDivide_EngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignDivide;OutEngineTimeFloat;EngineTime", funcOperatorAssignDivide_OutEngineTimeFloat_EngineTime );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorEqual;EngineTimeEngineTime;Bool", funcOperatorEqual_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNotEqual;EngineTimeEngineTime;Bool", funcOperatorNotEqual_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreater;EngineTimeEngineTime;Bool", funcOperatorGreater_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreaterEqual;EngineTimeEngineTime;Bool", funcOperatorGreaterEqual_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLess;EngineTimeEngineTime;Bool", funcOperatorLess_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLessEqual;EngineTimeEngineTime;Bool", funcOperatorLessEqual_EngineTimeEngineTime_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreater;EngineTimeFloat;Bool", funcOperatorGreater_EngineTimeFloat_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreaterEqual;EngineTimeFloat;Bool", funcOperatorGreaterEqual_EngineTimeFloat_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLess;EngineTimeFloat;Bool", funcOperatorLess_EngineTimeFloat_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLessEqual;EngineTimeFloat;Bool", funcOperatorLessEqual_EngineTimeFloat_Bool );
}