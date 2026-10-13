/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathMatrix.h"
#include "scriptStackFrame.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_NODEFAULT_TYPE( Matrix );
	RTTI_ALIGN_TYPE( 16 );

	RTTI_PROPERTY( X ).setName( "X" ).editable();
	RTTI_PROPERTY( Y ).setName( "Y" ).editable();
	RTTI_PROPERTY( Z ).setName( "Z" ).editable();
	RTTI_PROPERTY( W ).setName( "W" ).editable();

	// Matrix natives
	RTTI_NATIVE_STATIC_FUNCTION( "Identity", funcIdentity );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltTranslation", funcBuiltTranslation );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltRotation", funcBuiltRotation );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltScale", funcBuiltScale );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltPreScale", funcBuiltPreScale );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltTRS", funcBuiltTRS );
	RTTI_NATIVE_STATIC_FUNCTION( "BuiltRTS", funcBuiltRTS );
	RTTI_NATIVE_STATIC_FUNCTION( "BuildFromDirectionVector", funcBuildFromDirectionVector );
	RTTI_NATIVE_STATIC_FUNCTION( "GetTranslation", funcGetTranslation );
	RTTI_NATIVE_STATIC_FUNCTION( "GetRotation", funcGetRotation );
	RTTI_NATIVE_STATIC_FUNCTION( "GetScale", funcGetScale );
	RTTI_NATIVE_STATIC_FUNCTION( "GetAxisX", funcGetAxisX );
	RTTI_NATIVE_STATIC_FUNCTION( "GetAxisY", funcGetAxisY );
	RTTI_NATIVE_STATIC_FUNCTION( "GetAxisZ", funcGetAxisZ );
	RTTI_NATIVE_STATIC_FUNCTION( "GetDirectionVector", funcGetDirectionVector );
	RTTI_NATIVE_STATIC_FUNCTION( "GetInverted", funcGetInverted );
	RTTI_NATIVE_STATIC_FUNCTION( "GetInvertedFull", funcGetInvertedFull );
	RTTI_NATIVE_STATIC_FUNCTION( "ToQuat", funcToQuat );
	RTTI_NATIVE_STATIC_FUNCTION( "IsOk", funcIsOk );

RTTI_END_TYPE();

const Matrix Matrix::IDENTITY_CONSTANT( Matrix::IDENTITY() );

void Matrix::funcIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, Matrix::IDENTITY() );
}

void Matrix::funcBuiltTranslation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, move, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Matrix mat = Matrix::IDENTITY();
	mat.SetTranslation( move );
	RETURN_STRUCT( Matrix, mat );
}

void Matrix::funcBuiltRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, rot.ToMatrix() );
}

void Matrix::funcBuiltScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, scale, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Matrix mat = Matrix::IDENTITY();
	mat.SetScale33( scale );
	RETURN_STRUCT( Matrix, mat );
}

void Matrix::funcBuiltPreScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, scale, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Matrix mat = Matrix::IDENTITY();
	mat.SetPreScale33( scale );
	RETURN_STRUCT( Matrix, mat );
}

void Matrix::funcBuiltTRS( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( Vector4, move, Vector4::ZEROS() );
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	GET_PARAMETER_OPT( Vector4, scale, Vector4::ONES() );
	FINISH_PARAMETERS;

	Matrix t = Matrix::IDENTITY();
	t.SetTranslation( move );

	Matrix r = rot.ToMatrix();

	Matrix s = Matrix::IDENTITY();
	s.SetScale33( scale );

	Matrix ret = t * r * s;
	RETURN_STRUCT( Matrix, ret );
}

void Matrix::funcBuiltRTS( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( EulerAngles, rot, EulerAngles::ZEROS() );
	GET_PARAMETER_OPT( Vector4, move, Vector4::ZEROS() );
	GET_PARAMETER_OPT( Vector4, scale, Vector4::ONES() );
	FINISH_PARAMETERS;

	Matrix t = Matrix::IDENTITY();
	t.SetTranslation( move );

	Matrix r = rot.ToMatrix();

	Matrix s = Matrix::IDENTITY();
	s.SetScale33( scale );

	Matrix ret = r * t * s;
	RETURN_STRUCT( Matrix, ret );
}

void Matrix::funcBuildFromDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, dirVec, Vector4::ZEROS() );
	GET_PARAMETER_OPT( Vector4, upVec, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Matrix ret;
	upVec.IsZero() && upVec.IsOk() ? ret.BuildFromDirectionVector( dirVec ) : ret.BuildFromDirectionVector( dirVec, upVec );
	RETURN_STRUCT( Matrix, ret );
}

void Matrix::funcGetTranslation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetTranslation() );
}

void Matrix::funcGetAxisX( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetAxisX() );
}

void Matrix::funcGetAxisY( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetAxisY() );
}

void Matrix::funcGetAxisZ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetAxisZ() );
}

void Matrix::funcGetDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetAxisY() );
}

void Matrix::funcGetRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( EulerAngles, mat.ToEulerAnglesFull() );
}

void Matrix::funcGetScale( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, mat.GetScale33() );
}

void Matrix::funcGetInverted( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, mat.OrthonormInverted() );
}

void Matrix::funcGetInvertedFull( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, mat.FullInverted() );
}

void Matrix::funcToQuat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, mat.ToQuat() );
}

void Matrix::funcIsOk( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, mat, Matrix::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_BOOL( mat.IsOk() );
}

void funcOperatorMultiply_MatrixMatrix_Matrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, a, Matrix::ZEROS() );
	GET_PARAMETER( Matrix, b, Matrix::ZEROS() );	
	FINISH_PARAMETERS;
	RETURN_STRUCT( Matrix, Matrix::Mul( a, b ) );
}

void funcOperatorAssignMultiply_OutMatrixMatrix_Matrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Matrix, a, Matrix::ZEROS() );
	GET_PARAMETER( Matrix, b, Matrix::ZEROS() );	
	FINISH_PARAMETERS;
	RETURN_STRUCT( Matrix, a = Matrix::Mul( a, b ) );
}

void RegisterScriptCoreMatrixOperators()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;MatrixMatrix;Matrix", funcOperatorMultiply_MatrixMatrix_Matrix );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutMatrixMatrix;Matrix", funcOperatorAssignMultiply_OutMatrixMatrix_Matrix );
}