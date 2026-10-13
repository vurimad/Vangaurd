/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptingSystem.h"
#include "scriptStackFrame.h"
#include "mathUtils.h"
#include "../../redMath/include/random.h"
#include "../../redMath/include/random.h"
#include "rttiFunctionMacros.h"

/////////////////////////////////////////////
// Math functions
/////////////////////////////////////////////

void funcRand( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

	const auto randomNumber = (Int32)math::DefaultRandom().Get<Int32>();
	RETURN_INT( randomNumber );
}

void funcRandRange( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, min, 0 );
	GET_PARAMETER( Int32, max, 0 );
	FINISH_PARAMETERS;

	RED_FATAL_ASSERT( min <= max, "Error: Max must be greater than or equal to min" );

	const auto randomNumber = math::DefaultRandom().Get( min , max );
	RETURN_INT( randomNumber );
}

void funcRandDifferent( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, lastValue, 0 );
	GET_PARAMETER( Int32, range, 0 );
	FINISH_PARAMETERS;

	Int32 randomNumber = 0;

	if( range <= 1 )
	{
		randomNumber = 0;
	}
	else if( range > 2 || lastValue < 0 || lastValue >= range )
	{
		do
		{
			randomNumber = math::DefaultRandom().Get( range );
		}
		while( randomNumber == lastValue );
	}
	else // range == 2 and lastValue is within the range
	{
		randomNumber = ( ( lastValue + 1 ) % 2 );
	}

	RETURN_INT( randomNumber );
}

void funcRandF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

	RETURN_FLOAT( math::DefaultRandom().Get<Float>() );
}

void funcRandRangeF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, rangeMin, 0.0f );
	GET_PARAMETER( Float, rangeMax, 1.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( math::DefaultRandom().Get( rangeMin, rangeMax ) );
}

void funcRandNoiseF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, seed, 0 );
	GET_PARAMETER( Float, max, 0.0f );
	GET_PARAMETER_OPT( Float, min, 0.0f );
	FINISH_PARAMETERS;

	math::Random rand;
	rand.Seed( seed );

	RETURN_FLOAT( rand.Get( min, max ) );
}

void funcAbs( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, range, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Abs( range ) );
}

void funcMin( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, a, 0 );
	GET_PARAMETER( Int32, b, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Min<Int32>( a, b ) );
}

void funcMax( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, a, 0 );
	GET_PARAMETER( Int32, b, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Max<Int32>( a, b ) );
}

void funcClamp( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, v, 0 );
	GET_PARAMETER( Int32, a, 0 );
	GET_PARAMETER( Int32, b, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Clamp<Int32>( v, a, b ) );
}

void funcDeg2Rad( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, deg, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( DEG2RAD( deg ) );
}

void funcRad2Deg( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, rad, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( RAD2DEG( rad ) );
}

void funcAbsF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Abs<Float>( a ) );
}

void funcSinF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( MSin( a ) );
}

void funcAsinF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( asinf( a ) );
}

void funcCosF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( MCos( a ) );
}

void funcAcosF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( acosf( a ) );
}

void funcTanF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( tanf( a ) );
}

void funcAtanF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( atan2f( a, b ) );
}

void funcExpF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( expf( a ) );
}

void funcPowF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, x, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( powf( a, x ) );
}

void funcLogF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( logf( a ) );
}

void funcSqrtF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( sqrtf( a ) );
}

void funcSqrF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( a * a );
}

void funcCalcSeed( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( THandle< IScriptable>, objHanlde, NULL );
	FINISH_PARAMETERS;

	IScriptable* obj = objHanlde.Get();

	RETURN_INT( Int32( (uintptr_t)(void*)(obj) ) );
}

void funcMinF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Min<Float>( a, b ) );
}

void funcMaxF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Max<Float>( a, b ) );
}

void funcClampF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, v, 0.0f );
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, b, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( Clamp<Float>( v, a, b ) );
}

void funcLerpF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, alpha, 0.0f );
	GET_PARAMETER( Float, a, 0.0f );
	GET_PARAMETER( Float, b, 0.0f );
	GET_PARAMETER_OPT( Bool, clamp, false );
	FINISH_PARAMETERS;

	Float ret = math::Lerp<Float>( alpha, a, b );
	if ( clamp && ret < a ) ret = a;
	if ( clamp && ret > b ) ret = b;
	RETURN_FLOAT( ret );
}

void funcCeilF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_INT( (int)ceilf( a ) );
}

void funcFloorF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_INT( (int)MFloor( a ) );
}

void funcRoundF( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_INT( (int)a );
}

void funcRoundFEx( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, a, 0.0f );
	FINISH_PARAMETERS;

	RETURN_INT( (int)MRound( a ) );
}

void funcReinterpretIntAsFloat( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, var, 0 );
	FINISH_PARAMETERS;

	RETURN_FLOAT( *(Float*)(&var) );
}

/////////////////////////////////////////////
// Angle functions
/////////////////////////////////////////////

void funcAngleNormalize( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, angle, 0.0f );
	FINISH_PARAMETERS;

	RETURN_FLOAT( EulerAngles::NormalizeAngle( angle ) );
}

void funcAngleDistance( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, target, 0.0f );
	GET_PARAMETER( Float, current, 0.0f );
	FINISH_PARAMETERS;
	
	Float delta = EulerAngles::AngleDistance( current, target );
	RETURN_FLOAT( delta );
}

void funcAngleApproach( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, target, 0.0f );
	GET_PARAMETER( Float, current, 0.0f );
	GET_PARAMETER( Float, step, 0.0f );
	FINISH_PARAMETERS;

	Float delta = EulerAngles::AngleDistance( current, target );

	// Move
	if ( MAbs( delta ) > MAbs( step ) )
	{
		Float deltaSign = delta >= 0.0f ? 1.0f : -1.0f;
		current = EulerAngles::NormalizeAngle( current + step * deltaSign );
	}
	else
	{
		current = target; 
	} 

	// Return final angle
	RETURN_FLOAT( current );
}

//-----------------------------------------------------------------------

void funcInt8ToInt( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int8, i, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Int32( i ) );
}

void funcIntToInt8( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, i, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Int8( i ) );
}

void funcIntToUint64( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, i, 0 );
	FINISH_PARAMETERS;

	RETURN_UINT64( Uint64( i ) );
}

void funcUint64ToInt( IScriptable*,  CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Uint64, i, 0 );
	FINISH_PARAMETERS;

	RETURN_INT( Int32( i ) );
}

#define RTTI_DEFINE_NATIVE_MATH( x )	\
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( #x, func##x );

void RegisterCoreScriptScalarMath()
{
	// Scalar natives
	RTTI_DEFINE_NATIVE_MATH( Rand );
	RTTI_DEFINE_NATIVE_MATH( RandRange );
	RTTI_DEFINE_NATIVE_MATH( RandF );
	RTTI_DEFINE_NATIVE_MATH( RandRangeF );
	RTTI_DEFINE_NATIVE_MATH( RandNoiseF );
	RTTI_DEFINE_NATIVE_MATH( RandDifferent );

	RTTI_DEFINE_NATIVE_MATH( Abs );
	RTTI_DEFINE_NATIVE_MATH( Min );
	RTTI_DEFINE_NATIVE_MATH( Max );
	RTTI_DEFINE_NATIVE_MATH( Clamp );
	RTTI_DEFINE_NATIVE_MATH( Deg2Rad );
	RTTI_DEFINE_NATIVE_MATH( Rad2Deg );
	RTTI_DEFINE_NATIVE_MATH( AbsF );
	RTTI_DEFINE_NATIVE_MATH( SinF );
	RTTI_DEFINE_NATIVE_MATH( AsinF );
	RTTI_DEFINE_NATIVE_MATH( CosF );
	RTTI_DEFINE_NATIVE_MATH( AcosF );
	RTTI_DEFINE_NATIVE_MATH( TanF );
	RTTI_DEFINE_NATIVE_MATH( AtanF );
	RTTI_DEFINE_NATIVE_MATH( ExpF );
	RTTI_DEFINE_NATIVE_MATH( PowF );
	RTTI_DEFINE_NATIVE_MATH( LogF );
	RTTI_DEFINE_NATIVE_MATH( SqrtF );
	RTTI_DEFINE_NATIVE_MATH( SqrF );
	RTTI_DEFINE_NATIVE_MATH( CalcSeed );
	RTTI_DEFINE_NATIVE_MATH( MinF );
	RTTI_DEFINE_NATIVE_MATH( MaxF );
	RTTI_DEFINE_NATIVE_MATH( ClampF );
	RTTI_DEFINE_NATIVE_MATH( LerpF );
	RTTI_DEFINE_NATIVE_MATH( CeilF );
	RTTI_DEFINE_NATIVE_MATH( FloorF );
	RTTI_DEFINE_NATIVE_MATH( RoundF );
	RTTI_DEFINE_NATIVE_MATH( RoundFEx );
	RTTI_DEFINE_NATIVE_MATH( ReinterpretIntAsFloat );

	// Angle functions
	RTTI_DEFINE_NATIVE_MATH( AngleNormalize );
	RTTI_DEFINE_NATIVE_MATH( AngleDistance );
	RTTI_DEFINE_NATIVE_MATH( AngleApproach );

	// Conversions
	RTTI_DEFINE_NATIVE_MATH( Int8ToInt );
	RTTI_DEFINE_NATIVE_MATH( IntToInt8 );
	RTTI_DEFINE_NATIVE_MATH( IntToUint64 );
	RTTI_DEFINE_NATIVE_MATH( Uint64ToInt );
}