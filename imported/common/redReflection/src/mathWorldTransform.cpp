#include "build.h"
#include "mathWorldTransform.h"

#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptOpcodes.h"
#include "rttiClassBuilder.h"
#include "mathTransform.h"
#include "mathMatrix.h"
#include "mathEulerAngles.h"

RTTI_BEGIN_NODEFAULT_TYPE( WorldTransform );
RTTI_PROPERTY( m_position ).setName( "Position" ).instanceEditable().replicated( WorldPosition::GetStaticClass()->GetDefaultReplicatedType() /* this is to workaround math::WorldPosition which isn't RTTI'ed vs WorldPosition */ );
RTTI_PROPERTY( m_orientation ).setName( "Orientation" ).instanceEditable().replicated( Quaternion::GetStaticClass()->GetDefaultReplicatedType() /* this is to workaround math::Quaternion which isn't RTTI'ed vs Quaternion */ );

RTTI_NATIVE_STATIC_FUNCTION( "SetIdentity", funcSetIdentity );
RTTI_NATIVE_STATIC_FUNCTION( "SetWorldPosition", funcSetWorldPosition );
RTTI_NATIVE_STATIC_FUNCTION( "SetPosition", funcSetPosition );
RTTI_NATIVE_STATIC_FUNCTION( "SetOrientation", funcSetOrientation_Quat );
RTTI_NATIVE_STATIC_FUNCTION( "SetOrientationEuler", funcSetOrientation_Eulers );
RTTI_NATIVE_STATIC_FUNCTION( "SetOrientationFromDir", funcSetOrientation_Direction );

RTTI_NATIVE_STATIC_FUNCTION( "TransformXForm", funcTransformXForm );
RTTI_NATIVE_STATIC_FUNCTION( "TransformWorldXForm", funcTransformWorldXForm );

RTTI_NATIVE_STATIC_FUNCTION( "TransformPoint", funcTransformPoint );
RTTI_NATIVE_STATIC_FUNCTION( "TransformWorldPosition", funcTransformWorldPosition );

RTTI_NATIVE_STATIC_FUNCTION( "TransformInvWorldXForm", funcTransformInvWorldXForm );
RTTI_NATIVE_STATIC_FUNCTION( "TransformInvXForm", funcTransformInvXForm );
RTTI_NATIVE_STATIC_FUNCTION( "TransformInvPoint", funcTransformInvPoint );
RTTI_NATIVE_STATIC_FUNCTION( "TransformInvWorldPosition", funcTransformInvWorldPosition );

RTTI_NATIVE_STATIC_FUNCTION( "GetWorldPosition", funcGetWorldPosition );
RTTI_NATIVE_STATIC_FUNCTION( "GetOrientation", funcGetOrientation );

RTTI_NATIVE_STATIC_FUNCTION( "GetInverse", funcGetInverse );
RTTI_NATIVE_STATIC_FUNCTION( "GetForward", funcGetForward );
RTTI_NATIVE_STATIC_FUNCTION( "GetRight", funcGetRight );
RTTI_NATIVE_STATIC_FUNCTION( "GetUp", funcGetUp );
RTTI_NATIVE_STATIC_FUNCTION( "ToMatrix", funcToMatrix );
RTTI_NATIVE_STATIC_FUNCTION( "_ToXForm", funcToXForm );

RTTI_END_TYPE();


void WorldTransform::funcSetIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	t = WorldTransform::IDENTITY();
}

void WorldTransform::funcSetWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( WorldPosition, p, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;

	t.SetPosition( p );
}

void WorldTransform::funcSetPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Vector4, p, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	t.SetPosition( p );
}

void WorldTransform::funcSetOrientation_Quat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Quaternion, q, Quaternion::IDENTITY() );
	FINISH_PARAMETERS;

	t.SetOrientation( q );
}

void WorldTransform::funcSetOrientation_Eulers( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( EulerAngles, e, EulerAngles::ZEROS() );
	FINISH_PARAMETERS;

	t.SetOrientation( e.ToQuat() );
}

void WorldTransform::funcSetOrientation_Direction( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Vector4, v, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Quaternion q;
	q.BuildFromDirectionVector( v );
	t.SetOrientation( q );
}

void WorldTransform::funcTransformXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Transform, xform, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( WorldTransform, t.TransformXForm( xform ) );
}

void WorldTransform::funcTransformWorldXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( WorldTransform, xform, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( WorldTransform, t.TransformXForm( xform ) );
}

void WorldTransform::funcTransformPoint( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Vector4, p, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( WorldPosition, t.TransformPoint( p ) );
}

void WorldTransform::funcTransformWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( WorldPosition, p, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( WorldPosition, t.TransformPoint( p ) );
}

void WorldTransform::funcTransformInvWorldXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( WorldTransform, xform, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Transform, t.TransformInv( xform ) );
}

void WorldTransform::funcTransformInvXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Transform, xform, Transform::IDENTITY() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Transform, t.TransformInv( xform ) );
}

void WorldTransform::funcTransformInvPoint( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( Vector4, p, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	Vector4 out = t.TransformInvPoint( WorldPosition( p ) );
	RETURN_STRUCT( Vector4, out );
}

void WorldTransform::funcTransformInvWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	GET_PARAMETER( WorldPosition, p, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 out = t.TransformInvPoint( p );
	RETURN_STRUCT( Vector4, out );
}

void WorldTransform::funcGetWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( WorldPosition, t.GetPosition() );
}

void WorldTransform::funcGetOrientation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Quaternion, t.GetOrientation() );
}

void WorldTransform::funcGetInverse( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	
	RETURN_STRUCT( WorldTransform, t.GetInverse() );
}

void WorldTransform::funcGetForward( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 out = Vector4( t.GetForward(), 0.f );
	RETURN_STRUCT( Vector4, out );
}

void WorldTransform::funcGetRight( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 out = Vector4( t.GetRight(), 0.f );
	RETURN_STRUCT( Vector4, out );
}

void WorldTransform::funcGetUp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;

	const Vector4 out = Vector4( t.GetUp(), 0.f );
	RETURN_STRUCT( Vector4, out );
}

void WorldTransform::funcToMatrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Matrix, t.ToMatrix() );
}

void WorldTransform::funcToXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldTransform, t, WorldTransform::IDENTITY() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Transform, t._ToXForm() );
}

