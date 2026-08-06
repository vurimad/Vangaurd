/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathCommon.h"
#include "mathTransform.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptOpcodes.h"
#include "mathUtils.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_NODEFAULT_TYPE( Transform )
	RTTI_PROPERTY( m_position ).setName("position").instanceEditable().persistent().replicated( Vector4::GetStaticClass()->GetDefaultReplicatedType() /* this is to workaround math::Vector4 which isn't RTTI'ed vs Vector4 */ );
	RTTI_PROPERTY( m_orientation ).setName("orientation").instanceEditable().persistent().replicated( Quaternion::GetStaticClass()->GetDefaultReplicatedType() /* this is to workaround math::Vector4 which isn't RTTI'ed vs Vector4 */ );
	RTTI_NATIVE_STATIC_FUNCTION("TransformPoint", funcTransformPoint);
	RTTI_NATIVE_STATIC_FUNCTION("TransformVector", funcTransformVector);
	RTTI_NATIVE_STATIC_FUNCTION("ToEulerAngles", funcToEulerAngles);
	RTTI_NATIVE_STATIC_FUNCTION("ToMatrix", funcToMatrix);
	RTTI_NATIVE_STATIC_FUNCTION("GetForward", funcGetForward);
	RTTI_NATIVE_STATIC_FUNCTION("GetRight", funcGetRight);
	RTTI_NATIVE_STATIC_FUNCTION("GetUp", funcGetUp);
	RTTI_NATIVE_STATIC_FUNCTION("GetPitch", funcGetPitch);
	RTTI_NATIVE_STATIC_FUNCTION("GetYaw", funcGetYaw);
	RTTI_NATIVE_STATIC_FUNCTION("GetRoll", funcGetRoll);
	RTTI_NATIVE_STATIC_FUNCTION("SetIdentity", funcSetIdentity);
	RTTI_NATIVE_STATIC_FUNCTION("SetInverse", funcSetInverse);
	RTTI_NATIVE_STATIC_FUNCTION("GetInverse", funcGetInverse);
	RTTI_NATIVE_STATIC_FUNCTION("GetPosition", funcGetPosition);
	RTTI_NATIVE_STATIC_FUNCTION("GetOrientation", funcGetOrientation);
	RTTI_NATIVE_STATIC_FUNCTION("SetPosition", funcSetPosition);
	RTTI_NATIVE_STATIC_FUNCTION("SetOrientation", funcSetOrientation_Quat);
	RTTI_NATIVE_STATIC_FUNCTION("SetOrientationEuler", funcSetOrientation_Eulers);
	RTTI_NATIVE_STATIC_FUNCTION("SetOrientationFromDir", funcSetOrientation_Direction);
RTTI_END_TYPE();

void Transform::funcTransformPoint(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( Vector4, p, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, t.TransformPoint(p) );
}

void Transform::funcTransformVector(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, t.TransformVector(v) );
}

void Transform::funcToEulerAngles(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( EulerAngles, t.ToEulerAngles() );
}

void Transform::funcToMatrix(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Matrix, t.ToMatrix() );
}

void Transform::funcGetForward(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 fwd = t.GetForward();
	const Vector4 out{ fwd.X, fwd.Y, fwd.Z, 0.f };
	RETURN_STRUCT( Vector4, out );
}

void Transform::funcGetRight(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 right = t.GetRight();
	const Vector4 out{ right.X, right.Y, right.Z, 0.f };
	RETURN_STRUCT( Vector4, out );
}

void Transform::funcGetUp(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 up = t.GetUp();
	const Vector4 out = Vector4{ up.X, up.Y, up.Z, 0.f };
	RETURN_STRUCT( Vector4, out );
}

void Transform::funcGetPitch(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_FLOAT(t.GetPitch());
}

void Transform::funcGetYaw(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_FLOAT(t.GetYaw());
}

void Transform::funcGetRoll(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_FLOAT(t.GetRoll());
}

void Transform::funcSetIdentity(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	t.SetIdentity();
}

void Transform::funcSetInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	t.SetInverse();
}

void Transform::funcGetInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Transform, t.GetInverse() );
}

void Transform::funcGetPosition(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, t.GetPosition() );
}

void Transform::funcGetOrientation(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, t, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Quaternion, t.GetOrientation() );
}

void Transform::funcSetPosition(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( Vector4, p, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	t.SetPosition(p);

	RETURN_STRUCT( Transform, t );
}

void Transform::funcSetOrientation_Quat(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	t.SetOrientation(q);

	RETURN_STRUCT( Transform, t );
}

void Transform::funcSetOrientation_Eulers(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( EulerAngles, e, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	t.SetOrientation(e);

	RETURN_STRUCT( Transform, t );
}

void Transform::funcSetOrientation_Direction(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Transform, t, Transform::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	t.SetOrientation(v);

	RETURN_STRUCT( Transform, t );
}

void funcOperatorMultiply_TransformVector4_Vector4(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, xform, Transform::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, xform * v );
}

void funcOperatorMultiply_TransformTransform_Transform(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Transform, xform1, Transform::IDENTITY() );
	GET_PARAMETER( Transform, xform2, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Transform, xform1 * xform2 );
}

void registerScriptTransformOperators()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;TransformVector4;Vector4", funcOperatorMultiply_TransformVector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;TransformTransform;Transform", funcOperatorMultiply_TransformTransform_Transform );
}