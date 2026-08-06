/**
* Copyright (c) 2007-17 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptStackFrame.h"
#include "scriptOpcodes.h"
#include "rttiFunctionMacros.h"

//////////////////////////////////////////////////////////////////////////
// NOTE: Operators MUST conform to new naming strategy
// template is:
//   Operator<Type>_<Param1><Param2>_<Ret>
// examples:
//    OperatorAdd_Int32Int32_Int32
//    OperatorLogicAnd_BoolBool_SkipBool
//    etc
//
// See CScriptFunctionStub::RebuildOperatorName() for more details

//////////////////////////////////////////////////////////////////////////
// Arithmetic operators (+ compound assignment)

template < typename T >
void funcOperatorAdd( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a + b ) );
}

template < typename T >
void funcOperatorSubtract( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a - b ) );
}

template < typename T >
void funcOperatorMultiply( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a * b ) );
}

template < typename T >
void funcOperatorDivide( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( b ? ( a / b ) : 0 ) );
}

template < typename T >
void funcOperatorIntModulo( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( b ? ( a % b ) : 0 ) );
}

template < typename T >
void funcOperatorFloatModulo( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( fmod( a, b ) ) );
}

template < typename T >
void funcOperatorNeg( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( -a ) );
}

template < typename T >
void funcOperatorAssignAdd( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a += b ) );
}

template < typename T >
void funcOperatorAssignSubtract( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a -= b ) );
}

template < typename T >
void funcOperatorAssignMultiply( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a *= b ) );
}

template < typename T >
void funcOperatorAssignDivide( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( b ? ( a /= b ) : ( a = 0 ) ) );
}

// Register all arithmetic operators except:
// - modulo (which is different for integer and floating point types)
// - negation (which is not valid for signed types)
#define REGISTER_COMMON_ARITHMETIC_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;" #type #type ";" #type, funcOperatorAdd< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorSubtract;" #type #type ";" #type, funcOperatorSubtract< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorMultiply;" #type #type ";" #type, funcOperatorMultiply< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorDivide;" #type #type ";" #type, funcOperatorDivide< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;Out" #type #type ";" #type, funcOperatorAssignAdd< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignSubtract;Out" #type #type ";" #type, funcOperatorAssignSubtract< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignMultiply;Out" #type #type ";" #type, funcOperatorAssignMultiply< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignDivide;Out" #type #type ";" #type, funcOperatorAssignDivide< type > );

//////////////////////////////////////////////////////////////////////////
// Relational operators

template < typename T >
void funcOperatorEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a == b );
}

template < typename T >
void funcOperatorNotEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a != b );
}

template < typename T >
void funcOperatorLess( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a < b );
}

template < typename T >
void funcOperatorLessEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a <= b );
}

template < typename T >
void funcOperatorGreater( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a > b );
}

template < typename T >
void funcOperatorGreaterEqual( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_BOOL( a >= b );
}

#define REGISTER_RELATIONAL_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorEqual;" #type #type ";Bool", funcOperatorEqual< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNotEqual;" #type #type ";Bool", funcOperatorNotEqual< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLess;" #type #type ";Bool", funcOperatorLess< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLessEqual;" #type #type ";Bool", funcOperatorLessEqual< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreater;" #type #type ";Bool", funcOperatorGreater< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorGreaterEqual;" #type #type ";Bool", funcOperatorGreaterEqual< type > );

//////////////////////////////////////////////////////////////////////////
// Bitwise operators (+ compound assignment)

template < typename T >
void funcOperatorAnd( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a & b ) );
}

template < typename T >
void funcOperatorOr( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a | b ) );
}

template < typename T >
void funcOperatorXor( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a ^ b ) );
}

template < typename T >
void funcOperatorBitNot( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( T, a, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( ~a ) );
}

template < typename T >
void funcOperatorAssignAnd( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a &= b ) );
}

template < typename T >
void funcOperatorAssignOr( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( T, a, T( 0 ) );
	GET_PARAMETER( T, b, T( 0 ) );
	FINISH_PARAMETERS;
	RETURN_AUTO( T( a |= b ) );
}

#define REGISTER_BITWISE_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAnd;" #type #type ";" #type, funcOperatorAnd< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorOr;" #type #type ";"  #type, funcOperatorOr< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorXor;" #type #type ";"  #type, funcOperatorXor< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorBitNot;" #type ";"  #type, funcOperatorBitNot< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAnd;Out" #type #type ";"  #type, funcOperatorAssignAnd< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignOr;Out" #type #type ";"  #type, funcOperatorAssignOr< type > );

//////////////////////////////////////////////////////////////////////////
// Integer types operators

#define REGISTER_INT_OPERATORS( type ) \
static_assert( std::is_signed< type >::value && std::is_integral< type >::value, "Type needs to be signed integer" ); \
REGISTER_COMMON_ARITHMETIC_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorModulo;" #type #type ";" #type, funcOperatorIntModulo< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNeg;" #type ";" #type, funcOperatorNeg< type > ); \
REGISTER_RELATIONAL_OPERATORS( type ) \
REGISTER_BITWISE_OPERATORS( type )

#define REGISTER_UINT_OPERATORS( type ) \
static_assert( !std::is_signed< type >::value && std::is_integral< type >::value, "Type needs to be unsigned integer" ); \
REGISTER_COMMON_ARITHMETIC_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorModulo;" #type #type ";" #type, funcOperatorIntModulo< type > ); \
REGISTER_RELATIONAL_OPERATORS( type ) \
REGISTER_BITWISE_OPERATORS( type )

//////////////////////////////////////////////////////////////////////////
// Floating-point types operators

#define REGISTER_FLOAT_OPERATORS( type ) \
static_assert( std::is_floating_point< type >::value, "Type needs to be floating-point" ); \
REGISTER_COMMON_ARITHMETIC_OPERATORS( type ) \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorModulo;" #type #type ";" #type, funcOperatorFloatModulo< type > ); \
RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNeg;" #type ";" #type, funcOperatorNeg< type > ); \
REGISTER_RELATIONAL_OPERATORS( type )

//////////////////////////////////////////////////////////////////////////
// Logic operators

void funcOperatorLogicAnd_BoolSkipBool_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get first param
	GET_PARAMETER( Bool, a, false );

	// We expect a special op here
	RED_ASSERT( *stack.m_code == OP_Skip );
	stack.m_code++;

	// Get skip offset
	Uint16 offset = stack.Read< Uint16 >();

	// Value is determined by the first param, skip next
	if ( !a )
	{
		// Skip the param
		stack.m_code += offset;

		// We should skip the funcOperatortion by now
		RED_ASSERT( *stack.m_code == OP_ParamEnd );
		stack.m_code++;

		// Return false
		RETURN_BOOL( false );
	}
	else
	{
		// Evaluate the second parameter and use it as a result
		GET_PARAMETER( Bool, b, 0 );
		FINISH_PARAMETERS;
		RETURN_BOOL( b );
	}
}

void funcOperatorLogicOr_BoolSkipBool_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get first param
	GET_PARAMETER( Bool, a, false );

	// We expect a special op here
	RED_ASSERT( *stack.m_code == OP_Skip );
	stack.m_code++;

	// Get skip offset
	Uint16 offset = stack.Read< Uint16 >();

	// Value is determined by the first param, skip next
	if ( a )
	{
		// Skip the param
		stack.m_code += offset;

		// We should skip the funcOperatortion by now
		RED_ASSERT( *stack.m_code == OP_ParamEnd );
		stack.m_code++;

		// Return true
		RETURN_BOOL( true );
	}
	else
	{
		// Evaluate the second parameter and use it as a result
		GET_PARAMETER( Bool, b, 0 );
		FINISH_PARAMETERS;
		RETURN_BOOL( b );
	}
}

void funcOperatorLogicNot_Bool_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Bool, a, false );
	FINISH_PARAMETERS;
	RETURN_BOOL( !a );
}

//////////////////////////////////////////////////////////////////////////
// Custom string operators

void funcOperatorAdd_StringString_String( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, a, String::EMPTY() );
	GET_PARAMETER( String, b, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( a + b );
}

void funcOperatorAssignAdd_OutStringString_String( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( String, a, String::EMPTY() );
	GET_PARAMETER( String, b, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( a += b );
}

//////////////////////////////////////////////////////////////////////////
// Custom IScriptable operators

void funcOperatorEqual_IScriptableIScriptable_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( THandle< IScriptable >, a, nullptr );
	GET_PARAMETER( THandle< IScriptable >, b, nullptr );
	FINISH_PARAMETERS;
	RETURN_BOOL( a == b );
}

void funcOperatorNotEqual_IScriptableIScriptable_Bool( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( THandle< IScriptable >, a, nullptr );
	GET_PARAMETER( THandle< IScriptable >, b, nullptr );
	FINISH_PARAMETERS;
	RETURN_BOOL( a != b );
}

//////////////////////////////////////////////////////////////////////////
// finally, all definitions goes here

void RegisterCoreScriptOperators()
{
	// Common operators for integer and floating-point types
	REGISTER_INT_OPERATORS( Int8 );
	REGISTER_UINT_OPERATORS( Uint8 );
	REGISTER_INT_OPERATORS( Int16 );
	REGISTER_UINT_OPERATORS( Uint16 );
	REGISTER_INT_OPERATORS( Int32 );
	REGISTER_UINT_OPERATORS( Uint32 );
	REGISTER_INT_OPERATORS( Int64 );
	REGISTER_UINT_OPERATORS( Uint64 );
	REGISTER_FLOAT_OPERATORS( Float );
	REGISTER_FLOAT_OPERATORS( Double );

	// Logic operators
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLogicAnd;BoolSkipBool;Bool", funcOperatorLogicAnd_BoolSkipBool_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLogicOr;BoolSkipBool;Bool", funcOperatorLogicOr_BoolSkipBool_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorLogicNot;Bool;Bool", funcOperatorLogicNot_Bool_Bool );

	// Custom string operators
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAdd;StringString;String", funcOperatorAdd_StringString_String );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorAssignAdd;OutStringString;String", funcOperatorAssignAdd_OutStringString_String );

	// Custom IScriptable operators
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorEqual;IScriptableIScriptable;Bool", funcOperatorEqual_IScriptableIScriptable_Bool );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "OperatorNotEqual;IScriptableIScriptable;Bool", funcOperatorNotEqual_IScriptableIScriptable_Bool );
}