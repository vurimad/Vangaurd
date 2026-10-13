
//DONT CHANGE EXISTING IDS - IT WILL BREAK SAVED FUNCTIONS

//Values come off stack in reversed order

#define LOGICAL_OPERATORS_START_ID 128

OPERATOR_GROUP( ScalarOperations )
REGISTER_OPERATOR_INLINE( +, context.ReturnFloat( context.GetFloat() + context.GetFloat() ), 1, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( -, { Float temp = context.GetFloat(); context.ReturnFloat( context.GetFloat() - temp );}, 2, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( *, context.ReturnFloat( context.GetFloat() * context.GetFloat() ), 3, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( /, { Float temp = context.GetFloat(); context.ReturnFloat( context.GetFloat() / temp ); }, 4, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( ^, { Float temp = context.GetFloat(); context.ReturnFloat( pow( context.GetFloat(), temp ) ); }, 5, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( %, { Float temp = context.GetFloat(); context.ReturnFloat( fmod( context.GetFloat(), temp ) ); }, 6, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( sin, context.ReturnFloat( MSin( DEG2RAD( context.GetFloat() ) ) ), 7, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( cos, context.ReturnFloat( MCos( DEG2RAD( context.GetFloat() ) ) ), 8, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( tan, context.ReturnFloat( tan( DEG2RAD( context.GetFloat() ) ) ), 9, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( asin, context.ReturnFloat( RAD2DEG( asin( context.GetFloat() ) ) ), 10, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( acos, context.ReturnFloat( RAD2DEG( acos( context.GetFloat() ) ) ), 11, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( atan, context.ReturnFloat( RAD2DEG( atan( context.GetFloat() ) ) ), 12, VarScalar, VarScalar )

REGISTER_OPERATOR_INLINE( PI, context.ReturnFloat( RED_PI ), 13, VarScalar )
REGISTER_OPERATOR_INLINE( min, context.ReturnFloat( std::min( context.GetFloat(), context.GetFloat() ) ), 14, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( max, context.ReturnFloat( std::max( context.GetFloat(), context.GetFloat() ) ), 15, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( log, context.ReturnFloat( log( context.GetFloat() ) ), 16, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( log2, context.ReturnFloat( log( context.GetFloat() ) / log( 2.0f ) ), 17, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( sqrt, context.ReturnFloat( sqrt( context.GetFloat() ) ), 18, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( cbrt, context.ReturnFloat( cbrt( context.GetFloat() ) ), 19, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( abs, context.ReturnFloat( abs( context.GetFloat() ) ), 20, VarScalar, VarScalar )

REGISTER_OPERATOR_INLINE( ceil, context.ReturnFloat( ceil( context.GetFloat() ) ), 21, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( floor, context.ReturnFloat( floor( context.GetFloat() ) ), 22, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( round, context.ReturnFloat( round( context.GetFloat() ) ), 23, VarScalar, VarScalar )

REGISTER_OPERATOR_INLINE( #, context.ReturnFloat( -context.GetFloat() ), 24, VarScalar, VarScalar ) // unary minus

REGISTER_OPERATOR_INLINE( clamp, { const Float maxVal = context.GetFloat(); const Float minVal = context.GetFloat(); Float val = context.GetFloat();
context.ReturnFloat( Clamp<Float>( val, minVal, maxVal ) ); }, 25, VarScalar, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( sign, { const Float val = context.GetFloat(); val >= 0.0f ? context.ReturnFloat( 1.0f ) : context.ReturnFloat( -1.0f ); }, 26, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( Unused5, ;, 27, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( Unused6, ;, 28, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( Unused7, ;, 29, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( Unused8, ;, 30, VarInvalid, VarSize )


OPERATOR_GROUP( VectorOperations )
REGISTER_OPERATOR_INLINE( Vec, { Float z = context.GetFloat(); Float y = context.GetFloat(); Float x = context.GetFloat(); context.ReturnVector( Vector4( x,y,z ) ); }, 31, VarVector, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( -, { Vector4 v = context.GetVector(); context.ReturnVector( context.GetVector() - v );}, 32, VarVector, VarVector, VarVector )
REGISTER_OPERATOR_INLINE( +, context.ReturnVector( context.GetVector() + context.GetVector() ), 33, VarVector, VarVector, VarVector )

REGISTER_OPERATOR_INLINE( *, { Float f = context.GetFloat(); Vector4 v = context.GetVector(); context.ReturnVector( v*f ); }, 34, VarVector, VarVector, VarScalar )
REGISTER_OPERATOR_INLINE( *, { Vector4 v = context.GetVector(); Float f = context.GetFloat(); context.ReturnVector( v*f ); }, 35, VarVector, VarScalar, VarVector )
REGISTER_OPERATOR_INLINE( /, { Float f = context.GetFloat(); Vector4 v = context.GetVector(); context.ReturnVector( v/f ); }, 36, VarVector, VarVector, VarScalar )
REGISTER_OPERATOR_INLINE( /, { Vector4 v = context.GetVector(); Float f = context.GetFloat(); context.ReturnVector( v/f ); }, 37, VarVector, VarScalar, VarVector )

REGISTER_OPERATOR_INLINE( dot,   { Vector4 v1 = context.GetVector(); Vector4 v2 = context.GetVector(); context.ReturnFloat( ::Vector4::Dot3( v2, v1 ) ); }, 38, VarScalar, VarVector, VarVector )
REGISTER_OPERATOR_INLINE( cross, { Vector4 v1 = context.GetVector(); Vector4 v2 = context.GetVector(); context.ReturnVector( ::Vector4::Cross( v2, v1 ) ); }, 39, VarVector, VarVector, VarVector )

REGISTER_OPERATOR_INLINE( length,   { context.ReturnFloat( context.GetVector().Mag3() ); }, 40, VarScalar, VarVector )
REGISTER_OPERATOR_INLINE( lengthSq, { context.ReturnFloat( context.GetVector().SquareMag3() ); }, 41, VarScalar, VarVector )
REGISTER_OPERATOR_INLINE( norm, { context.ReturnVector( context.GetVector().Normalized3() ); }, 42, VarVector, VarVector )

REGISTER_OPERATOR_INLINE( lerp, { Float w = context.GetFloat(); Vector4 v2 = context.GetVector(); Vector4 v1 = context.GetVector(); context.ReturnVector( Lerp( w, v1, v2 ) ); }, 43, VarVector, VarVector, VarVector, VarScalar )

REGISTER_OPERATOR_INLINE( setX, { Float f = context.GetFloat(); auto v = context.GetVector(); v.X = f; context.ReturnVector(v); }, 44, VarVector, VarVector, VarScalar )
REGISTER_OPERATOR_INLINE( setY, { Float f = context.GetFloat(); auto v = context.GetVector(); v.Y = f; context.ReturnVector(v); }, 45, VarVector, VarVector, VarScalar )
REGISTER_OPERATOR_INLINE( setZ, { Float f = context.GetFloat(); auto v = context.GetVector(); v.Z = f; context.ReturnVector(v); }, 46, VarVector, VarVector, VarScalar )

REGISTER_OPERATOR_INLINE( setXY, { Float f1 = context.GetFloat(); Float f2 = context.GetFloat(); context.ReturnVector( Vector4( f2, f1, context.GetVector().Z ) ); }, 47, VarVector, VarVector, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( setYZ, { Float f1 = context.GetFloat(); Float f2 = context.GetFloat(); context.ReturnVector( Vector4( context.GetVector().X, f2, f1 ) ); }, 48, VarVector, VarVector, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( setXZ, { Float f1 = context.GetFloat(); Float f2 = context.GetFloat(); context.ReturnVector( Vector4( f2, context.GetVector().Y, f1 ) ); }, 49, VarVector, VarVector, VarScalar, VarScalar )

REGISTER_OPERATOR_INLINE( getX, { context.ReturnFloat( context.GetVector().X ); }, 50, VarScalar, VarVector )
REGISTER_OPERATOR_INLINE( getY, { context.ReturnFloat( context.GetVector().Y ); }, 51, VarScalar, VarVector )
REGISTER_OPERATOR_INLINE( getZ, { context.ReturnFloat( context.GetVector().Z ); }, 52, VarScalar, VarVector )

REGISTER_OPERATOR_INLINE( UnusedV1, ;, 53, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV2, ;, 54, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV3, ;, 55, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV4, ;, 56, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV5, ;, 57, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV6, ;, 58, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV7, ;, 59, VarInvalid, VarSize )
REGISTER_OPERATOR_INLINE( UnusedV8, ;, 60, VarInvalid, VarSize )


OPERATOR_GROUP( RotationOperations )
REGISTER_OPERATOR_INLINE( Rot, { Float f = context.GetFloat(); Vector4 v = context.GetVector(); context.ReturnQuaternion( Quaternion( v, DEG2RAD( f ) ) ); }, 61, VarRotation, VarVector, VarScalar )
REGISTER_OPERATOR_INLINE( Rot, { Quaternion yawQ( Vector3::EZ(), DEG2RAD( context.GetFloat() ) ); Quaternion pitchQ( Vector3::EX(), DEG2RAD( context.GetFloat() ) ); Quaternion rollQ( Vector3::EY(), DEG2RAD( context.GetFloat() ) );   return context.ReturnQuaternion( yawQ * pitchQ * rollQ ); }, 62, VarRotation, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( getRoll, { return context.ReturnFloat( context.GetQuaternion().GetRoll() ); }, 63, VarScalar, VarRotation );
REGISTER_OPERATOR_INLINE( getPitch, { return context.ReturnFloat( context.GetQuaternion().GetPitch() ); }, 64, VarScalar, VarRotation );
REGISTER_OPERATOR_INLINE( getYaw, { return context.ReturnFloat( context.GetQuaternion().GetYaw() ); }, 65, VarScalar, VarRotation );

OPERATOR_GROUP( LogicalOperations )
REGISTER_OPERATOR_INLINE( &, context.ReturnFloat( ExpressionToolkitHelpers::AndOperation( context.GetFloat(), context.GetFloat() ) ), LOGICAL_OPERATORS_START_ID, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( |, context.ReturnFloat( ExpressionToolkitHelpers::OrOperation( context.GetFloat(), context.GetFloat() ) ), LOGICAL_OPERATORS_START_ID + 1, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( xor, context.ReturnFloat( ExpressionToolkitHelpers::XorOperation( context.GetFloat(), context.GetFloat() ) ), LOGICAL_OPERATORS_START_ID + 2, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( !, context.ReturnFloat( ExpressionToolkitHelpers::NotOperation( context.GetFloat() ) ), LOGICAL_OPERATORS_START_ID + 3, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( >, { const Float op2 = context.GetFloat();  context.ReturnFloat( ExpressionToolkitHelpers::GreaterThanOperation( context.GetFloat(), op2 ) ); };, LOGICAL_OPERATORS_START_ID + 4, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( greater_or_equal, { const Float op2 = context.GetFloat();  context.ReturnFloat( ExpressionToolkitHelpers::GreaterThanOrEqualOperation( context.GetFloat(), op2 ) ); };, LOGICAL_OPERATORS_START_ID + 5, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( <, { const Float op2 = context.GetFloat();  context.ReturnFloat( ExpressionToolkitHelpers::LessThanOperation( context.GetFloat(), op2 ) ); };, LOGICAL_OPERATORS_START_ID + 6, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( less_or_equal, { const Float op2 = context.GetFloat();  context.ReturnFloat( ExpressionToolkitHelpers::LessThanOrEqualOperation( context.GetFloat(), op2 ) ); };, LOGICAL_OPERATORS_START_ID + 7, VarScalar, VarScalar, VarScalar )
REGISTER_OPERATOR_INLINE( =, context.ReturnFloat( ExpressionToolkitHelpers::EqualOperation( context.GetFloat(), context.GetFloat() ) );, LOGICAL_OPERATORS_START_ID + 8, VarScalar, VarScalar, VarScalar )
