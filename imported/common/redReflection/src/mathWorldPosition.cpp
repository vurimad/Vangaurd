/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "mathWorldPosition.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptOpcodes.h"
#include "mathVector3.h"
#include "rttiClassBuilder.h"

//
RTTI_BEGIN_TYPE( FixedPoint );
RTTI_PROPERTY( m_bits ).setName( "Bits" ).instanceEditable().replicated();
RTTI_END_TYPE();

RTTI_BEGIN_NODEFAULT_TYPE( WorldPosition );
RTTI_PROPERTY( x ).setName( "x" ).instanceEditable().replicated( ::FixedPoint::GetStaticClass()->GetDefaultReplicatedType() );
RTTI_PROPERTY( y ).setName( "y" ).instanceEditable().replicated( ::FixedPoint::GetStaticClass()->GetDefaultReplicatedType() );
RTTI_PROPERTY( z ).setName( "z" ).instanceEditable().replicated( ::FixedPoint::GetStaticClass()->GetDefaultReplicatedType() );

RTTI_NATIVE_STATIC_FUNCTION( "SetX", funcSetX );
RTTI_NATIVE_STATIC_FUNCTION( "SetY", funcSetY );
RTTI_NATIVE_STATIC_FUNCTION( "SetZ", funcSetZ );
RTTI_NATIVE_STATIC_FUNCTION( "SetXYZ", funcSetXYZ );
RTTI_NATIVE_STATIC_FUNCTION( "SetVector4", funcSetVector4 );
RTTI_NATIVE_STATIC_FUNCTION( "GetX", funcGetX );
RTTI_NATIVE_STATIC_FUNCTION( "GetY", funcGetY );
RTTI_NATIVE_STATIC_FUNCTION( "GetZ", funcGetZ );
RTTI_NATIVE_STATIC_FUNCTION( "ToVector4", funcToVector4 );
RTTI_END_TYPE();


#define REDREFLECTION_WORLD_POSITION_SET_ELEM( elem ) \
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );\
	GET_PARAMETER( Float, b, 0.f );\
	FINISH_PARAMETERS;\
	a.elem = FixedPoint( b )

void WorldPosition::funcSetX( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	REDREFLECTION_WORLD_POSITION_SET_ELEM( x );
}

void WorldPosition::funcSetY( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	REDREFLECTION_WORLD_POSITION_SET_ELEM( y );
}

void WorldPosition::funcSetZ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	REDREFLECTION_WORLD_POSITION_SET_ELEM( z );
}

void WorldPosition::funcSetXYZ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Float, x, 0.f );
	GET_PARAMETER( Float, y, 0.f );
	GET_PARAMETER( Float, z, 0.f );
	FINISH_PARAMETERS;
	a = WorldPosition( Vector3( x, y, z ) );
}

void WorldPosition::funcSetVector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Vector4, xyz, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;
	a = WorldPosition( xyz.AsVector3() );
}

void WorldPosition::funcGetX( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_FLOAT( a.x.AsFloat() );
}

void WorldPosition::funcGetY( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_FLOAT( a.y.AsFloat() );
}

void WorldPosition::funcGetZ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_FLOAT( a.z.AsFloat() );
}

void WorldPosition::funcToVector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, Vector4( a.AsVector3(), 1.0f ) );
}

// --- operators funcOperator[name]_[return]_[args]
void funcOperatorAdd_WorldPositionVector4_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( WorldPosition, a + b );
}
void funcOperatorSubtract_WorldPositionVector4_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( WorldPosition, a - b );
}

void funcOperatorAssignAdd_OutWorldPositionVector4_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;
	a += b;
	RETURN_STRUCT( WorldPosition, a );
}

void funcOperatorAssignAdd_OutWorldPositionWorldPosition_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( WorldPosition, b, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	a = a + b;
	RETURN_STRUCT( WorldPosition, a );
}

void funcOperatorAssignSubtract_OutWorldPositionWorldPosition_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( WorldPosition, b, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	a = WorldPosition( a - b );
	RETURN_STRUCT( WorldPosition, a );
}

void funcOperatorAssignSubtract_OutWorldPositionVector4_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;
	a -= b;
	RETURN_STRUCT( WorldPosition, a );
}

void funcOperatorAdd_WorldPositionWorldPosition_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( WorldPosition, b, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( WorldPosition, a + b );
}

void funcOperatorSubtract_WorldPositionWorldPosition_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	GET_PARAMETER( WorldPosition, b, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a - b );
}

void funcOperatorNeg_WorldPosition_WorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( WorldPosition, a, WorldPosition::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( WorldPosition, -a );
}

void registerScriptWorldPositionOperators()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;WorldPositionVector4;WorldPosition", funcOperatorAdd_WorldPositionVector4_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;WorldPositionVector4;WorldPosition", funcOperatorSubtract_WorldPositionVector4_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutWorldPositionVector4;WorldPosition", funcOperatorAssignAdd_OutWorldPositionVector4_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutWorldPositionVector4;WorldPosition", funcOperatorAssignSubtract_OutWorldPositionVector4_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutWorldPositionWorldPosition;WorldPosition", funcOperatorAssignAdd_OutWorldPositionWorldPosition_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutWorldPositionWorldPosition;WorldPosition", funcOperatorAssignSubtract_OutWorldPositionWorldPosition_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;WorldPositionWorldPosition;WorldPosition", funcOperatorAdd_WorldPositionWorldPosition_WorldPosition );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;WorldPositionWorldPosition;Vector4", funcOperatorSubtract_WorldPositionWorldPosition_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNeg;WorldPosition;WorldPosition", funcOperatorNeg_WorldPosition_WorldPosition );
}