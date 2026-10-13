/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/


//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../redContainers/include/string/tokenizer.h"
#include "scriptingSystem.h"
#include "scriptStackFrame.h"
#include "rttiEnum.h"
#include "rttiSystem.h"
#include "rttiFunctionMacros.h"

//////////////////////////////////////////////////////////////////////////
// namespaces
using red::String;
using red::DynArray;

void funcLog( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	// Print

	RED_LOG_CATEGORY( red::LoggerCategory_Scripts, "%hs", text.AsChar() );
	
	RETURN_VOID();
}

void funcLogError( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	// Print

	RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Scripts, "%hs", text.AsChar() );

	RETURN_VOID();
}

void funcLogWarning( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	// Print

	RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Scripts, "%hs", text.AsChar() );

	RETURN_VOID();
}

void funcLogChannel( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, channel, CName::NONE() );
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	RED_LOG_CATEGORY( red::LoggerCategory_Scripts, "Script|%hs: %hs", channel.AsChar(), text.AsChar() );
	
	RETURN_VOID();
}

void funcLogChannelError( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, channel, CName::NONE() );
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Scripts, "Script|%hs: %hs", channel.AsChar(), text.AsChar() );

	RETURN_VOID();
}

void funcLogChannelWarning( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, channel, CName::NONE() );
	GET_PARAMETER( String, text, String::EMPTY() );
	FINISH_PARAMETERS;

	RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Scripts, "Script|%hs: %hs", channel.AsChar(), text.AsChar() );

	RETURN_VOID();
}

void funcTrace( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
#if !defined( RED_CONFIGURATION_FINAL )
	stack.DumpToLog();
#endif
	RETURN_VOID();
}

void funcTraceToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
	char buffer[ 1024 ];
	buffer[ 0 ] = '\0';
#if !defined( RED_CONFIGURATION_FINAL )
	stack.DumpTopToString( buffer, RED_ARRAY_COUNT_U32( buffer ) );
#endif
	RETURN_STRING( buffer );
}

void funcDebugBreak( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
	RETURN_VOID();
}

void funcLoadResource( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, unsafePath, String::EMPTY() );
	GET_PARAMETER_OPT( Bool, isDepotPath, false );
	FINISH_PARAMETERS;

	RED_ASSERT( !"Loading resources from scripts is not supported any more" );
	RETURN_OBJECT( nullptr );
}

void funcLoadResourceAsync( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, unsafePath, String::EMPTY() );
	GET_PARAMETER_OPT( Bool, isDepotPath, false );
	FINISH_PARAMETERS;

	RED_ASSERT( !"Loading resources from scripts is not supported any more" );
	RETURN_OBJECT( nullptr );
}

void funcDumpClassHierarchy( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, className, CName::NONE() );
	FINISH_PARAMETERS;
	RETURN_BOOL( false );
}

void funcArraySortInts( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( DynArray< Int32 >, items, red::PoolScript() );
	FINISH_PARAMETERS;

	std::sort( items.Begin(), items.End() );

	RETURN_VOID();
}

void funcArraySortFloats( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( DynArray< Float >, items, red::PoolScript() );
	FINISH_PARAMETERS;

	std::sort( items.Begin(), items.End() );

	RETURN_VOID();
}

void funcArraySortStrings( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( DynArray< String >, items, red::PoolScript());
	FINISH_PARAMETERS;

	std::sort( items.Begin(), items.End() );

	RETURN_VOID();
}

void funcUint64ToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Uint64, i, 0 );
	FINISH_PARAMETERS;

	RETURN_STRING( String::Printf( "%llu", i ) );
}

void funcEnumGetMax( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, name, CName::NONE() );
	FINISH_PARAMETERS;

	Int64 max = 0, v = 0;
	const rtti::EnumType *e = GetRttiSystem().FindScriptEnum( name );
	if ( e )
	{
		const DynArray< CName >& options = e->GetOptions();
		for ( Uint32 i = 0; i < options.Size(); ++i )
		{
			if ( e->FindValue( options[ i ], v ) && v > max)
			{
				max = v;
			}
		}
	}

	RETURN_INT64( max );
}

void funcEnumGetMin( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, name, CName::NONE() );
	FINISH_PARAMETERS;

	Int64 min = 0, v = 0;
	const rtti::EnumType *e = GetRttiSystem().FindScriptEnum( name );
	if ( e )
	{
		const DynArray< CName >& options = e->GetOptions();
		for ( Uint32 i = 0; i < options.Size(); ++i )
		{
			if ( e->FindValue( options[ i ], v ) && v < min)
			{
				min = v;
			}
		}
	}

	RETURN_INT64( min );
}

void funcEnumValueFromString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, enumTypeStr, String::EMPTY() );
	GET_PARAMETER( String, enumValueStr, String::EMPTY() );
	FINISH_PARAMETERS;

	const rtti::EnumType* enumType = GetRttiSystem().FindScriptEnum( RED_NAME_NOREG( enumTypeStr ) );
	if( !enumType )
	{
		RED_ASSERT( "EnumValueFromString: '%hs' is an invalid enum type!", enumTypeStr.AsChar() );
		RETURN_INT64( -1 );
		return;
	}

	Int64 enumValue = 0;
	if( !enumType->FindValue( RED_NAME_NOREG( enumValueStr ), enumValue ) )
	{
		RED_ASSERT( "EnumValueFromString: '%hs' is an invalid value for enum '%hs'", enumValueStr.AsChar(), enumTypeStr.AsChar() );
		RETURN_INT64( -1 );
		return;
	}

	RETURN_INT64( enumValue );
}

void funcEnumValueToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, enumTypeStr, String::EMPTY() );
	GET_PARAMETER( Uint64, enumValue, 0 );
	FINISH_PARAMETERS;

	const rtti::EnumType* enumType = GetRttiSystem().FindScriptEnum( RED_NAME_NOREG( enumTypeStr ) );
	if( !enumType )
	{
		RED_ASSERT( "EnumValueToString: '%hs' is an invalid enum type!", enumTypeStr.AsChar() );
		RETURN_STRING( String::EMPTY() );
		return;
	}

	CName enumValueName;
	if( !enumType->FindName( enumValue, enumValueName ) )
	{
		String valueStr = String::Printf( "%llu", enumValue );
		RED_ASSERT( "EnumValueToString: '%hs' is an invalid value for enum '%hs'", valueStr.AsChar(), enumTypeStr.AsChar() );
		RETURN_STRING( String::EMPTY() );
		return;
	}

	RETURN_STRING( enumValueName.AsChar() );
}

void funcEnumValueFromName( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, enumTypeName, CName::NONE() );
	GET_PARAMETER( CName, enumValueName, CName::NONE() );
	FINISH_PARAMETERS;

	const rtti::EnumType* enumType = GetRttiSystem().FindScriptEnum( enumTypeName );
	if( !enumType )
	{
		RED_ASSERT( "EnumValueFromString: '%hs' is an invalid enum type!", enumTypeName.AsChar() );
		RETURN_INT64( -1 );
		return;
	}

	Int64 enumValue = 0;
	if( !enumType->FindValue( enumValueName, enumValue ) )
	{
		RED_ASSERT( "EnumValueFromString: '%hs' is an invalid value for enum '%hs'", enumValueName.AsChar(), enumTypeName.AsChar() );
		RETURN_INT64( -1 );
		return;
	}

	RETURN_INT64( enumValue );
}

void funcEnumValueToName( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, enumTypeName, CName::NONE() );
	GET_PARAMETER( Uint64, enumValue, 0 );
	FINISH_PARAMETERS;

	const rtti::EnumType* enumType = GetRttiSystem().FindScriptEnum( enumTypeName );
	if( !enumType )
	{
		RED_ASSERT( "EnumValueToString: '%hs' is an invalid enum type!", enumTypeName.AsChar() );
		RETURN_NAME( CName::NONE() );
		return;
	}

	CName enumValueName;
	if( !enumType->FindName( enumValue, enumValueName ) )
	{
		String valueStr = String::Printf( "%llu", enumValue );
		RED_ASSERT( "EnumValueToString: '%hs' is an invalid value for enum '%hs'", valueStr.AsChar(), enumTypeName.AsChar() );
		RETURN_NAME( CName::NONE() );
		return;
	}

	RETURN_NAME( enumValueName );
}

void funcCompareArrayNameContents( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_REF( red::DynArray< CName >, first, red::PoolScript() );
	GET_PARAMETER_REF( red::DynArray< CName >, second, red::PoolScript() );
	FINISH_PARAMETERS;

	const auto firstSize = first.Size();
	const auto secondSize = second.Size();

	if ( !firstSize || firstSize != secondSize )
	{
		RETURN_BOOL( false );
		return;
	}

	// I know that this implementation is broken and it won't work properly for example for first=[a,a,b], second=[a,b,c]
	// but guess what... "correct" version break something in AI signal propagation!
	// Correct version should looks like this:
	// std::sort( first.Begin(), first.End() );
	// std::sort( second.Begin(), second.End() );
	// RETURN_BOOL( first == second );

	for( Uint32 i = 0; i < firstSize; i += 1 )
	{
		if( !first.FindPtr( second[i] ) )
		{
			RETURN_BOOL( false );
			return;
		}
	}

	RETURN_BOOL( true );
}

void funcIsFinal( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

#if defined( RED_CONFIGURATION_FINAL )
	RETURN_BOOL( true );
#else
	RETURN_BOOL( false );
#endif
}

void funcUseProfiler( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;

#if defined( USE_PROFILER )
	RETURN_BOOL( true );
#else
	RETURN_BOOL( false );
#endif
}

void RegisterCoreScriptFunctions()
{
	// Native math support
	extern void RegisterCoreScriptOperators();
	RegisterCoreScriptOperators();

	// Global casts
	extern void RegisterCoreScriptCastFunctions();
	RegisterCoreScriptCastFunctions();

	// Math
	extern void RegisterCoreScriptScalarMath();
	RegisterCoreScriptScalarMath();

	extern void registerScriptVector4Operators();
	registerScriptVector4Operators();

	extern void RegisterScriptCoreMatrixOperators();
	RegisterScriptCoreMatrixOperators();

	extern void registerScriptQuaternionOperators();
	registerScriptQuaternionOperators();

	extern void registerScriptTransformOperators();
	registerScriptTransformOperators();

	extern void registerScriptWorldPositionOperators();
	registerScriptWorldPositionOperators();

	// string natives
	extern void RegisterCoreScriptStringNatives();
	RegisterCoreScriptStringNatives();

	// Engine time stuff
	extern void RegisterScriptEngineTimeNatives();
	RegisterScriptEngineTimeNatives();

	// General
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "Log", funcLog );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LogError", funcLogError );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LogWarning", funcLogWarning );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LogChannel", funcLogChannel );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LogChannelError", funcLogChannelError );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LogChannelWarning", funcLogChannelWarning );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "TraceToString", funcTraceToString );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "Trace", funcTrace );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "DebugBreak", funcDebugBreak );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LoadResourceAsync", funcLoadResourceAsync );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "LoadResource", funcLoadResource );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "DumpClassHierarchy", funcDumpClassHierarchy );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "Uint64ToString", funcUint64ToString );

	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "ArraySortInts", funcArraySortInts );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "ArraySortFloats", funcArraySortFloats );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "ArraySortStrings", funcArraySortStrings );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "CompareArrayNameContents", funcCompareArrayNameContents );

	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumGetMax", funcEnumGetMax );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumGetMin", funcEnumGetMin );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumValueFromString", funcEnumValueFromString );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumValueToString", funcEnumValueToString );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumValueFromName", funcEnumValueFromName );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "EnumValueToName", funcEnumValueToName );

	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "IsFinal", funcIsFinal );
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "UseProfiler", funcUseProfiler );
}
