/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathCommon.h"
#include "scriptStackFrame.h"
#include "mathQuaternion.h"
#include "rttiClassBuilder.h"
#include "../../redMath/include/random.h"

RTTI_BEGIN_NODEFAULT_TYPE( Quaternion );
	RTTI_PROPERTY( i ).setName( "i" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( j ).setName( "j" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( k ).setName( "k" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( r ).setName( "r" ).instanceEditable().replicated().persistent();

	RTTI_NATIVE_STATIC_FUNCTION("SetIdentity", funcSetIdentity);
	RTTI_NATIVE_STATIC_FUNCTION("SetInverse", funcSetInverse);
	RTTI_NATIVE_STATIC_FUNCTION("GetXAxis", funcGetXAxis);
	RTTI_NATIVE_STATIC_FUNCTION("GetYAxis", funcGetYAxis);
	RTTI_NATIVE_STATIC_FUNCTION("GetZAxis", funcGetZAxis);
	RTTI_NATIVE_STATIC_FUNCTION("GetForward", funcGetForward);
	RTTI_NATIVE_STATIC_FUNCTION("GetRight", funcGetRight);
	RTTI_NATIVE_STATIC_FUNCTION("GetUp", funcGetUp);
	RTTI_NATIVE_STATIC_FUNCTION("ToMatrix", funcToMatrix);
	RTTI_NATIVE_STATIC_FUNCTION("ToEulerAngles", funcToEulerAngles);
	RTTI_NATIVE_STATIC_FUNCTION("GetAxes", funcGetAxes);
	RTTI_NATIVE_STATIC_FUNCTION("Rand", funcRand);
	RTTI_NATIVE_STATIC_FUNCTION("Dot", funcDot);
	RTTI_NATIVE_STATIC_FUNCTION("Transform", funcTransform);
	RTTI_NATIVE_STATIC_FUNCTION("TransformInverse", funcTransformInverse);
	RTTI_NATIVE_STATIC_FUNCTION("Normalize", funcNormalize);
	RTTI_NATIVE_STATIC_FUNCTION("Normalized", funcNormalized);
	RTTI_NATIVE_STATIC_FUNCTION("SetInverse", funcSetInverse);
	RTTI_NATIVE_STATIC_FUNCTION("SetShortestRotation", funcSetShortestRotation);
	RTTI_NATIVE_STATIC_FUNCTION("SetAxisAngle", funcSetAxisAngle);
	RTTI_NATIVE_STATIC_FUNCTION("SetXRot", funcSetXRot);
	RTTI_NATIVE_STATIC_FUNCTION("SetYRot", funcSetYRot);
	RTTI_NATIVE_STATIC_FUNCTION("SetZRot", funcSetZRot);
	RTTI_NATIVE_STATIC_FUNCTION("Lerp", funcLerp);
	RTTI_NATIVE_STATIC_FUNCTION("Slerp", funcSlerp);
	RTTI_NATIVE_STATIC_FUNCTION("GetAngle", funcGetAngle);
	RTTI_NATIVE_STATIC_FUNCTION("GetAxis", funcGetAxis);
	RTTI_NATIVE_STATIC_FUNCTION("Magnitude", funcMagnitude);
	RTTI_NATIVE_STATIC_FUNCTION("MagnitudeSq", funcMagnitudeSq);
	RTTI_NATIVE_STATIC_FUNCTION("MulInverse", funcMulInverse);
	RTTI_NATIVE_STATIC_FUNCTION("Conjugate", funcConjugate);
	RTTI_NATIVE_STATIC_FUNCTION("BuildFromDirectionVector", funcBuildFromDirectionVector);
RTTI_END_TYPE();

void Quaternion::funcGetXAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetXAxis4());
}

void Quaternion::funcGetYAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetYAxis4());
}

void Quaternion::funcGetZAxis(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetZAxis4());
}

void Quaternion::funcGetForward(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetYAxis4());
}

void Quaternion::funcGetRight(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetXAxis4());
}

void Quaternion::funcGetUp(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, rot.GetZAxis4());
}

void Quaternion::funcToMatrix(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_STRUCT(Matrix, rot.ToMatrix());
}

void Quaternion::funcToEulerAngles( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT( Quaternion, rot, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( EulerAngles, rot.ToEulerAngles() );
}

void Quaternion::funcGetAxes(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, rot, Quaternion::IDENTITY());
	GET_PARAMETER_REF(Vector4, forward, Vector4::ZEROS());
	GET_PARAMETER_REF(Vector4, right, Vector4::ZEROS());
	GET_PARAMETER_REF(Vector4, up, Vector4::ZEROS());
	FINISH_PARAMETERS;

	forward = rot.GetYAxis4();
	right = rot.GetXAxis4();
	up = rot.GetZAxis4();

	RETURN_VOID();
}

void Quaternion::funcDot(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_OPT(Quaternion, a, Quaternion::IDENTITY());
	GET_PARAMETER_OPT(Quaternion, b, Quaternion::IDENTITY());
	FINISH_PARAMETERS;

	RETURN_FLOAT(a.Dot(b));
}

void Quaternion::funcRand(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER(Float, rotMin, 0.0f);
	GET_PARAMETER(Float, rotMax, 0.0f);
	FINISH_PARAMETERS;

	EulerAngles rot = EulerAngles::ZEROS();
	rot.Pitch = math::DefaultRandom().Get<Float>(rotMin, rotMax);
	rot.Yaw = math::DefaultRandom().Get<Float>(rotMin, rotMax);

	auto quat = rot.math::EulerAngles::ToQuat();
	RETURN_STRUCT(Quaternion, quat);
}

void Quaternion::funcSetIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF(Quaternion, q, Quaternion::IDENTITY());
	FINISH_PARAMETERS;
	
	q.SetIdentity();
}

void Quaternion::funcTransform( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, q.Transform(v) );
}

void Quaternion::funcTransformInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT(Vector4, q.TransformInverse(v) );
}

void Quaternion::funcSetInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, a, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	a.SetInverse();
}

void Quaternion::funcSetShortestRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Vector4, from, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Vector4, to, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	q.SetShortestRotation(from, to);
}

void Quaternion::funcSetAxisAngle( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Vector4, vec, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Float, angle, 0.f );
	FINISH_PARAMETERS;

	q.SetAxisAngle(vec, angle);
}

void Quaternion::funcSetXRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, angle, 0.f );
	FINISH_PARAMETERS;

	q.SetXRot(angle);
}

void Quaternion::funcSetYRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, angle, 0.f );
	FINISH_PARAMETERS;

	q.SetYRot(angle);
}

void Quaternion::funcSetZRot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, angle, 0.f );
	FINISH_PARAMETERS;

	q.SetZRot(angle);
}

void Quaternion::funcLerp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, a, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, b, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, t, 0.f );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, Quaternion::Lerp( a, b, t ) );
}

void Quaternion::funcSlerp( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, a, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, b, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, t, 0.f );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, Quaternion::Slerp( a, b, t ) );
}

void Quaternion::funcGetAngle( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( q.GetAngle() );
}

void Quaternion::funcGetAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, q.GetAxis4() );
}

void Quaternion::funcNormalize(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	q.Normalize();
}

void Quaternion::funcNormalized(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, q.Normalized() );
}

void Quaternion::funcMagnitude(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Float, q.Magnitude() );
}

void Quaternion::funcMagnitudeSq(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Float, q.MagnitudeSq() );
}

void Quaternion::funcMulInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, q1.MulInverse(q2) );
}

void Quaternion::funcConjugate(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, q.Conjugate() );
}

void Quaternion::funcBuildFromDirectionVector( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, direction, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER_OPT( Vector4, up, Vector4::UP() );
	FINISH_PARAMETERS;

	Quaternion q( Quaternion::IDENTITY() );
	q.BuildFromDirectionVector( direction, up );

	RETURN_STRUCT( Quaternion, q );
}

void funcOperatorNeg_Quaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );	
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q.Neg() );
}

void funcOperatorAdd_QuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q1 + q2 );
}

void funcOperatorSubtract_QuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q1 - q2 );
}

void funcOperatorMultiply_QuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q1 * q2 );
}

void funcOperatorMultiply_QuaternionFloat_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, f, 1.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q * f );
}

void funcOperatorDivide_QuaternionFloat_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, f, 1.f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, q / f );
}

void funcOperatorAssignAdd_OutQuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	q1 += q2;
	RETURN_STRUCT(Quaternion, q1);
}

void funcOperatorAssignSubtract_OutQuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	q1 -= q2;
	RETURN_STRUCT(Quaternion, q1);
}

void funcOperatorAssignMultiply_OutQuaternionQuaternion_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q1, Quaternion::IDENTITY() );
	GET_PARAMETER( Quaternion, q2, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;
	q1 *= q2;
	RETURN_STRUCT(Quaternion, q1);
}

void funcOperatorAssignMultiply_OutQuaternionFloat_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, f, 1.f );
	FINISH_PARAMETERS;
	q *= f;
	RETURN_STRUCT(Quaternion, q);
}

void funcOperatorAssignDivide_OutQuaternionFloat_Quaternion(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Float, f, 1.f );
	FINISH_PARAMETERS;
	q /= f;
	RETURN_STRUCT(Quaternion, q);
}

void funcOperatorMultiply_QuaternionVector4_Vector4(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	v = q * v;
	RETURN_STRUCT( Vector4, v );
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

void registerScriptQuaternionOperators()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNeg;Quaternion;Quaternion", funcOperatorNeg_Quaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;QuaternionQuaternion;Quaternion", funcOperatorAdd_QuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;QuaternionQuaternion;Quaternion", funcOperatorSubtract_QuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;QuaternionQuaternion;Quaternion", funcOperatorMultiply_QuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;QuaternionFloat;Quaternion", funcOperatorMultiply_QuaternionFloat_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorDivide;QuaternionFloat;Quaternion", funcOperatorDivide_QuaternionFloat_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutQuaternionQuaternion;Quaternion", funcOperatorAssignAdd_OutQuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutQuaternionQuaternion;Quaternion", funcOperatorAssignSubtract_OutQuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutQuaternionQuaternion;Quaternion", funcOperatorAssignMultiply_OutQuaternionQuaternion_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutQuaternionFloat;Quaternion", funcOperatorAssignMultiply_OutQuaternionFloat_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignDivide;OutQuaternionFloat;Quaternion", funcOperatorAssignDivide_OutQuaternionFloat_Quaternion );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;QuaternionVector4;Vector4", funcOperatorMultiply_QuaternionVector4_Vector4 );
}
