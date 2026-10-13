/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathVector4.h"
#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptOpcodes.h"
#include "mathUtils.h"
#include "rttiClassBuilder.h"
#include "../../redMath/include/random.h"

RTTI_BEGIN_NODEFAULT_TYPE( Vector4 );
	RTTI_ALIGN_TYPE( 16 );
	RTTI_PROPERTY( X ).setName( "X" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( Y ).setName( "Y" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( Z ).setName( "Z" ).instanceEditable().replicated().persistent();
	RTTI_PROPERTY( W ).setName( "W" ).instanceEditable().replicated().persistent();
	
	// Vector natives
	RTTI_NATIVE_STATIC_FUNCTION( "Dot2D", funcDot2D );
	RTTI_NATIVE_STATIC_FUNCTION( "Dot", funcDot );
	RTTI_NATIVE_STATIC_FUNCTION( "Cross", funcCross );
	RTTI_NATIVE_STATIC_FUNCTION( "Length2D", funcLength2D );
	RTTI_NATIVE_STATIC_FUNCTION( "Length", funcLength );
	RTTI_NATIVE_STATIC_FUNCTION( "LengthSquared", funcLengthSquared );
	RTTI_NATIVE_STATIC_FUNCTION( "Normalize2D", funcNormalize2D );
	RTTI_NATIVE_STATIC_FUNCTION( "Normalize", funcNormalize );
	RTTI_NATIVE_STATIC_FUNCTION( "Rand2D", funcRand2D );
	RTTI_NATIVE_STATIC_FUNCTION( "Rand", funcRand );
	RTTI_NATIVE_STATIC_FUNCTION( "Mirror", funcMirror );
	RTTI_NATIVE_STATIC_FUNCTION( "Distance", funcDistance );
	RTTI_NATIVE_STATIC_FUNCTION( "DistanceSquared", funcDistanceSquared );
	RTTI_NATIVE_STATIC_FUNCTION( "Distance2D", funcDistance2D );
	RTTI_NATIVE_STATIC_FUNCTION( "DistanceSquared2D", funcDistanceSquared2D );
	RTTI_NATIVE_STATIC_FUNCTION( "DistanceToEdge", funcDistanceToEdge );
	RTTI_NATIVE_STATIC_FUNCTION( "NearestPointOnEdge", funcNearestPointOnEdge );
	RTTI_NATIVE_STATIC_FUNCTION( "ToRotation", funcToRotation );
	RTTI_NATIVE_STATIC_FUNCTION( "Heading", funcHeading );
	RTTI_NATIVE_STATIC_FUNCTION( "FromHeading", funcFromHeading );
	RTTI_NATIVE_STATIC_FUNCTION( "Transform", funcTransform );
	RTTI_NATIVE_STATIC_FUNCTION( "TransformDir", funcTransformDir );
	RTTI_NATIVE_STATIC_FUNCTION( "TransformH", funcTransformH );
	RTTI_NATIVE_STATIC_FUNCTION( "GetAngleBetween", funcGetAngleBetween );
	RTTI_NATIVE_STATIC_FUNCTION( "GetAngleDegAroundAxis", funcGetAngleDegAroundAxis );
	RTTI_NATIVE_STATIC_FUNCTION( "ProjectPointToPlane", funcProjectPointToPlane );
	RTTI_NATIVE_STATIC_FUNCTION( "RotateAxis", funcRotateAxis );

RTTI_END_TYPE();

Bool ToString( red::String& outTxt, const Vector4& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );
	red::String wAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToString( xAsString, val.X ) &&
		::ToString( yAsString, val.Y ) &&
		::ToString( zAsString, val.Z ) &&
		::ToString( wAsString, val.W ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += " ";
		vecAsString += zAsString;
		vecAsString += " ";
		vecAsString += wAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}

Bool ToStringMaxPrecision( red::String& outTxt, const Vector4& val )
{
	red::String xAsString = String_CreateExternal_OnStack( 128 );
	red::String yAsString = String_CreateExternal_OnStack( 128 );
	red::String zAsString = String_CreateExternal_OnStack( 128 );
	red::String wAsString = String_CreateExternal_OnStack( 128 );

	if ( ::ToStringMaxPrecision( xAsString, val.X ) &&
		::ToStringMaxPrecision( yAsString, val.Y ) &&
		::ToStringMaxPrecision( zAsString, val.Z ) &&
		::ToStringMaxPrecision( wAsString, val.W ) )
	{
		red::String vecAsString = String_CreateExternal_OnStack( 512 );
		vecAsString += "[";
		vecAsString += xAsString;
		vecAsString += " ";
		vecAsString += yAsString;
		vecAsString += " ";
		vecAsString += zAsString;
		vecAsString += " ";
		vecAsString += wAsString;
		vecAsString += "]";
		outTxt = vecAsString;
		return true;
	}
	return false;
}

void Vector4::funcDot2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Vector4::Dot2( a, b ) );
}

void Vector4::funcDot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Vector4::Dot3( a, b ) );
}

void Vector4::funcCross( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, Vector4::Cross( a, b, 1.0f ) );
}

void Vector4::funcLength2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.Mag2() );
}

void Vector4::funcLength( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.Mag3() );
}

void Vector4::funcLengthSquared( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.SquareMag3() );
}

void Vector4::funcNormalize2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, a.Normalized2() );
}

void Vector4::funcNormalize( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, a.Normalized3() );
}

void Vector4::funcRand2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

	Vector4 ret;
	do 
	{
		Float x = math::DefaultRandom().Get<Float>( -1.0f , 1.0f );
		Float y = math::DefaultRandom().Get<Float>( -1.0f , 1.0f );
		ret = Vector4( x, y, 0.0f );
	}
	while ( Vector4::Near3( ret, Vector4::ZEROS() ) );

	RETURN_STRUCT( Vector4, ret.Normalized2() );
}

void Vector4::funcRand( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

	Vector4 ret;
	do 
	{
		Float x = math::DefaultRandom().Get<Float>( -1.0f , 1.0f );
		Float y = math::DefaultRandom().Get<Float>( -1.0f , 1.0f );
		Float z = math::DefaultRandom().Get<Float>( -1.0f , 1.0f );
		ret = Vector4( x, y, z );
	}
	while ( Vector4::Near3( ret, Vector4::ZEROS() ) );

	RETURN_STRUCT( Vector4, ret.Normalized3() );
}

void Vector4::funcMirror( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, dir, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, normal, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Float d = 2.0f * Vector4::Dot3( dir, normal );
	Vector4 ret = dir - ( normal * d );

	RETURN_STRUCT( Vector4, ret );
}

void Vector4::funcDistance( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.DistanceTo( b ) );
}

void Vector4::funcDistanceSquared( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.DistanceSquaredTo( b ) );
}

void Vector4::funcDistance2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.DistanceTo2D( b ) );
}

void Vector4::funcDistanceSquared2D( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a.DistanceSquaredTo2D( b ) );
}

void Vector4::funcDistanceToEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, pt, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( pt.DistanceToEdge( a, b ) );
}

void Vector4::funcNearestPointOnEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, pt, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, pt.NearestPointOnEdge( a, b ) );
}

void Vector4::funcToRotation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, dir, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( EulerAngles, dir.ToEulerAnglesInversePitch() );
}

void Vector4::funcHeading( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, dir, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( dir.ToEulerAnglesInversePitch().Yaw );
}

void Vector4::funcFromHeading( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, heading, 0.0f );
	FINISH_PARAMETERS;

	const Float cosYaw = MCos( DEG2RAD( heading ) );
	const Float sinYaw = MSin( DEG2RAD( heading ) );
	const Vector4 ret( -sinYaw, cosYaw, 0.0f );

	RETURN_STRUCT( Vector4, ret );
}

void Vector4::funcTransform( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, m, Matrix::IDENTITY() );
	GET_PARAMETER( Vector4, point, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, m.TransformPoint( point ) );
}

void Vector4::funcRotateAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, vector, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, axis, Vector4::ZEROS() );
	GET_PARAMETER( Float, angle, 0.0f );
	FINISH_PARAMETERS;

    const math::Quaternion q( axis, angle);
    const Vector4 v = q.Transform(vector);
	RETURN_STRUCT( Vector4, v );
}

void Vector4::funcTransformDir( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, m, Matrix::IDENTITY()  );
	GET_PARAMETER( Vector4, dir, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_STRUCT( Vector4, m.TransformVector( dir ) );
}

void Vector4::funcTransformH( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Matrix, m, Matrix::IDENTITY() );
	GET_PARAMETER( Vector4, point, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 p = m.TransformVectorWithW( point );
	if ( p[3] ) p /= p[3];
	RETURN_STRUCT( Vector4, p );
}

void Vector4::funcGetAngleBetween( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, from, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, to, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( MathUtils::VectorUtils::GetAngleDegBetweenVectors( from, to ) );
}

void Vector4::funcGetAngleDegAroundAxis( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, dirA, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, dirB, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, axis, Vector4::EZ() );
	FINISH_PARAMETERS;

	RETURN_FLOAT( MathUtils::VectorUtils::GetAngleDegAroundAxis( dirA, dirB, axis ) );
}

void Vector4::funcProjectPointToPlane( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, p1, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, p2, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, p3, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, toProject, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	Vector4 res = Plane( p1, p2, p3 ).Project( toProject );
	RETURN_STRUCT( Vector4, res );
}

////////////////////////////////
// Vector4 operators
////////////////////////////////
void funcOperatorNeg_Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );	
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, -a );	
}

// ...Vector4Vector4
void funcOperatorAdd_Vector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a + b );	
}

void funcOperatorSubtract_Vector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a - b );	
}

void funcOperatorMultiply_Vector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a * b );	
}

void funcOperatorDivide_Vector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a / b );	
}

void funcOperatorAssignAdd_OutVector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	a += b;
	RETURN_STRUCT( Vector4, a );	
}

void funcOperatorAssignSubtract_OutVector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	a -= b;
	RETURN_STRUCT( Vector4, a );	
}

void funcOperatorAssignMultiply_OutVector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	a *= b;
	RETURN_STRUCT( Vector4, a );	
}

void funcOperatorAssignDivide_OutVector4Vector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	FINISH_PARAMETERS;
	a /= b;
	RETURN_STRUCT( Vector4, a );	
}

// ...Vector4Float
void funcOperatorAdd_Vector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a + b );	
}

void funcOperatorSubtract_Vector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a - b );	
}

void funcOperatorMultiply_Vector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a * b );	
}

void funcOperatorDivide_Vector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, a / b );	
}

void funcOperatorAssignAdd_OutVector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	a += b;
	RETURN_STRUCT( Vector4, a );
}

void funcOperatorAssignSubtract_OutVector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	a -= b;
	RETURN_STRUCT( Vector4, a );	
}

void funcOperatorAssignMultiply_OutVector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	a *= b;
	RETURN_STRUCT( Vector4, a );	
}

void funcOperatorAssignDivide_OutVector4Float_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;
	a /= b;
	RETURN_STRUCT( Vector4, a );	
}

// ...FloatVector4
void funcOperatorMultiply_FloatVector4_Vector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );	
	FINISH_PARAMETERS;
	RETURN_STRUCT( Vector4, b * a );
}

void funcv_SetInterpolate( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	RED_UNUSED( context );

	GET_PARAMETER( Vector4, a, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Vector4, b, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Float, w, 0.f );
	FINISH_PARAMETERS;

	Vector4 r = Vector4::Interpolate( a, b, w );

	RETURN_STRUCT( Vector4, r );
}

void funcv_SetRotatedDir( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	RED_UNUSED( context );

	GET_PARAMETER( Vector4, quat, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Vector4, vec, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	simd::Vector4 vec1;
	simd::Vector4 r1;

	math::Quaternion quat1(quat.X, quat.Y, quat.Z, quat.W);
	vec1 = reinterpret_cast< const simd::Vector4& >( vec );

	r1.RotateDirection( quat1, vec1 );

	Vector4 r = reinterpret_cast< const Vector4& >( r1 );
	RETURN_STRUCT( Vector4, r );
}

void funcv_SetTransformedPos( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	RED_UNUSED( context );

	GET_PARAMETER( simd::QsTransform, trans, simd::QsTransform() );
	GET_PARAMETER( Vector4, vec, Vector4::ZERO_3D_POINT() );
	FINISH_PARAMETERS;

	simd::QsTransform trans1;
	simd::Vector4 vec1;
	simd::Vector4 r1;

	trans1 = reinterpret_cast< const simd::QsTransform& >( trans );
	vec1 = reinterpret_cast< const simd::Vector4& >( vec );

	r1.SetTransformedPos( trans1, vec1 );

	Vector4 r = reinterpret_cast< const Vector4& >( r1 );
	RETURN_STRUCT( Vector4, r );
}

void funcv_ZeroElement( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	RED_UNUSED( context );
	RED_UNUSED( result );

	GET_PARAMETER_REF( Vector4, a, Vector4::ZERO_3D_POINT() );
	GET_PARAMETER( Int32, i, 0 );
	FINISH_PARAMETERS;

	a.ZeroElement( i );
}

#define RTTI_DEFINE_ENGINE_NATIVE_MATH( x )	\
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( #x, func##x );

void registerScriptVector4Operators()
{
	// Vector4 operators
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNeg;Vector4;Vector4", funcOperatorNeg_Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;Vector4Vector4;Vector4", funcOperatorAdd_Vector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;Vector4Vector4;Vector4", funcOperatorSubtract_Vector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;Vector4Vector4;Vector4", funcOperatorMultiply_Vector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorDivide;Vector4Vector4;Vector4", funcOperatorDivide_Vector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutVector4Vector4;Vector4", funcOperatorAssignAdd_OutVector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutVector4Vector4;Vector4", funcOperatorAssignSubtract_OutVector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutVector4Vector4;Vector4", funcOperatorAssignMultiply_OutVector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignDivide;OutVector4Vector4;Vector4", funcOperatorAssignDivide_OutVector4Vector4_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;Vector4Float;Vector4", funcOperatorAdd_Vector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;Vector4Float;Vector4", funcOperatorSubtract_Vector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;Vector4Float;Vector4", funcOperatorMultiply_Vector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorDivide;Vector4Float;Vector4", funcOperatorDivide_Vector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutVector4Float;Vector4", funcOperatorAssignAdd_OutVector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;OutVector4Float;Vector4", funcOperatorAssignSubtract_OutVector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;OutVector4Float;Vector4", funcOperatorAssignMultiply_OutVector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignDivide;OutVector4Float;Vector4", funcOperatorAssignDivide_OutVector4Float_Vector4 );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;FloatVector4;Vector4", funcOperatorMultiply_FloatVector4_Vector4 );

	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "v;SetInterpolate", funcv_SetInterpolate );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "v;SetRotatedDir", funcv_SetRotatedDir );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "v;SetTransformedPos", funcv_SetTransformedPos );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "v;ZeroElement", funcv_ZeroElement );
}