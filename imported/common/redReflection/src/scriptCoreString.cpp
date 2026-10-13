/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "scriptingSystem.h"
#include "scriptStackFrame.h"

#include "../../redContainers/include/string/string.h"
#include "../../redContainers/include/fundamentalStringConversion.h"
#include "rttiFunctionMacros.h"

/////////////////////////////////////////////
// string functions
/////////////////////////////////////////////

void funcStrLen( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_INT( str.Length() );
}

void funcStrCmp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, with, String::EMPTY() );
	GET_PARAMETER( Int32, count, -1 );
	GET_PARAMETER( Bool, noCase, false );
	FINISH_PARAMETERS;

	if ( noCase )
	{
		if ( count != -1 )
		{
			count = ::Clamp< Int32 >( count, 0, str.Length() );
			RETURN_INT( red::StrcmpNC( str.AsChar(), with.AsChar(), count ) );
		}
		else
		{
			RETURN_INT( red::StrcmpNC( str.AsChar(), with.AsChar() ) );
		}
	}
	else
	{
		if ( count != -1 )
		{
			count = ::Clamp< Int32 >( count, 0, str.Length() );
			RETURN_INT( red::Strcmp( str.AsChar(), with.AsChar(), count ) );
		}
		else
		{
			RETURN_INT( red::Strcmp( str.AsChar(), with.AsChar() ) );
		}
	}
}

void funcStrFindFirst( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;

	Uint32 index;
	if ( str.IndexOf( match, index ) )
	{
		RETURN_INT( static_cast< Int32 >( index ) );
	}
	else
	{
		RETURN_INT( -1 );
	}
}

void funcStrFindLast( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;

	Uint32 index;
	if ( str.IndexOfLast( match, index ) )
	{
		RETURN_INT( static_cast< Int32 >( index ) );
	}
	else
	{
		RETURN_INT( -1 );
	}
}

void funcStrSplitFirst( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, div, String::EMPTY() );
	GET_PARAMETER_REF( String, left, String::EMPTY() );
	GET_PARAMETER_REF( String, right, String::EMPTY() );
	FINISH_PARAMETERS;

	bool status = str.Split( div, &left, &right );
	RETURN_BOOL( status );
}

void funcStrSplitLast( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, div, String::EMPTY() );
	GET_PARAMETER_REF( String, left, String::EMPTY() );
	GET_PARAMETER_REF( String, right, String::EMPTY() );
	FINISH_PARAMETERS;

	bool status = str.SplitFromRight( div, &left, &right );
	RETURN_BOOL( status );
}

void funcStrSplit( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, div, String::EMPTY() );
	GET_PARAMETER( Bool, includeEmpty, false );
	FINISH_PARAMETERS;

	red::DynArray< String > split = str.Split( div, includeEmpty );
	RETURN_MOVE( std::move( split ) );
}

void funcStrReplace( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	GET_PARAMETER( String, with, String::EMPTY() );
	FINISH_PARAMETERS;

	str.Replace( match, with );
	RETURN_STRING( str );
}

void funcStrReplaceAll( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	GET_PARAMETER( String, with, String::EMPTY() );
	FINISH_PARAMETERS;

	str.ReplaceAll( match, with );
	RETURN_STRING( str );
}

void funcStrMid( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( Int32, start, 0 );
	GET_PARAMETER_OPT( Int32, length, 1000000 );
	FINISH_PARAMETERS;
	RETURN_STRING( str.MidString( start, length ) );
}

void funcStrLeft( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( Int32, length, 0 );
	FINISH_PARAMETERS;
	RETURN_STRING( str.LeftString( length ) );
}

void funcStrRight( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( Int32, length, 0 );
	FINISH_PARAMETERS;
	RETURN_STRING( str.RightString( length ) );
}

void funcStrBeforeFirst( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.StringBefore( match ) );
}

void funcStrBeforeLast( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.StringBeforeFromRight( match ) );
}

void funcStrAfterFirst( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.StringAfter( match ) );
}

void funcStrAfterLast( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.StringAfterFromRight( match ) );
}

void funcStrBeginsWith( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_BOOL( str.BeginsWith( match ) );
}

void funcStrEndsWith( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER( String, match, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_BOOL( str.EndsWith( match ) );
}

void funcStrUpper( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.ToUpper() );
}

void funcStrLower( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	FINISH_PARAMETERS;
	RETURN_STRING( str.ToLower() );
}

void funcStrChar( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, chr, 0 );
	FINISH_PARAMETERS;
	if ( chr < 32 ) chr = 32;
	RETURN_STRING( String( (char)chr ) );
}

void funcNameToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( CName, name, CName::NONE() );
	FINISH_PARAMETERS;

	const auto nameView = name.AsStringView();

	RETURN_STRING( nameView.ToString() );
}

void funcStringToName( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	FINISH_PARAMETERS;

	const CName name = RED_NAME( str ); // NOTE must register string or persistence system will not have required strings
	RETURN_NAME( name );
}

void funcFloatToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, var, 0.f );
	FINISH_PARAMETERS;

	RETURN_STRING( ToStringDirect( var ) );
}

void funcFloatToStringPrec( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Float, var, 0.f );
	GET_PARAMETER( Int32, precision, 1 );
	FINISH_PARAMETERS;

	RED_ASSERT( precision >= 0 );

	String format;
	const float epsilon = 0.00001f;
	if ( MAbs(var - MFloor(var)) < epsilon )
	{
		format = ("%.0f");
	}
	else
	{
		String temp = ToStringDirect( precision );
		format = ("%.") + temp + ("f");
	}

	RETURN_STRING( String::Printf( format.AsChar(), var ) );
}

void funcIntToString( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, var, 0 );
	FINISH_PARAMETERS;

	RETURN_STRING( ToStringDirect( var ) );
}

void funcStringToInt( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER_OPT( Int32, varDef, 0 );
	FINISH_PARAMETERS;

	Int32 var = 0;
	if ( !FromString( str, var ) )
	{
		var = varDef;
	}

	RETURN_INT( var );
}

void funcStringToUint64( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER_OPT( Uint64, varDef, 0 );
	FINISH_PARAMETERS;

	Uint64 var = 0;
	if ( !FromString( str, var ) )
	{
		var = varDef;
	}

	RETURN_UINT64( var );
}

void funcStringToFloat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( String, str, String::EMPTY() );
	GET_PARAMETER_OPT( Float, varDef, 0.f );
	FINISH_PARAMETERS;

	Float var = 0.f;
	if ( !FromString( str, var ) )
	{
		var = varDef;
	}

	RETURN_FLOAT( var );
}

void funcIsStringNumber(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType)
{
	GET_PARAMETER(String, str, String::EMPTY());
	FINISH_PARAMETERS;

	Float var = 0.f;
	Bool returnValue = false;

	returnValue = FromString(str, var);

	RETURN_BOOL(returnValue);
}

#define RTTI_DEFINE_NATIVE_FUNC( x )	\
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( #x, func##x );

void RegisterCoreScriptStringNatives()
{
	RTTI_DEFINE_NATIVE_FUNC( StrLen );
	RTTI_DEFINE_NATIVE_FUNC( StrCmp );
	RTTI_DEFINE_NATIVE_FUNC( StrFindFirst );
	RTTI_DEFINE_NATIVE_FUNC( StrFindLast );
	RTTI_DEFINE_NATIVE_FUNC( StrSplitFirst );
	RTTI_DEFINE_NATIVE_FUNC( StrSplitLast );
	RTTI_DEFINE_NATIVE_FUNC( StrSplit );
	RTTI_DEFINE_NATIVE_FUNC( StrReplace );
	RTTI_DEFINE_NATIVE_FUNC( StrReplaceAll );
	RTTI_DEFINE_NATIVE_FUNC( StrMid );
	RTTI_DEFINE_NATIVE_FUNC( StrLeft );
	RTTI_DEFINE_NATIVE_FUNC( StrRight );
	RTTI_DEFINE_NATIVE_FUNC( StrBeforeFirst );
	RTTI_DEFINE_NATIVE_FUNC( StrBeforeLast );
	RTTI_DEFINE_NATIVE_FUNC( StrAfterFirst );
	RTTI_DEFINE_NATIVE_FUNC( StrAfterLast );
	RTTI_DEFINE_NATIVE_FUNC( StrBeginsWith );
	RTTI_DEFINE_NATIVE_FUNC( StrEndsWith );
	RTTI_DEFINE_NATIVE_FUNC( StrUpper );
	RTTI_DEFINE_NATIVE_FUNC( StrLower );
	RTTI_DEFINE_NATIVE_FUNC( StrChar );
	RTTI_DEFINE_NATIVE_FUNC( NameToString );
	RTTI_DEFINE_NATIVE_FUNC( StringToName );
	RTTI_DEFINE_NATIVE_FUNC( FloatToString );
	RTTI_DEFINE_NATIVE_FUNC( FloatToStringPrec );
	RTTI_DEFINE_NATIVE_FUNC( IntToString );
	RTTI_DEFINE_NATIVE_FUNC( StringToInt );
	RTTI_DEFINE_NATIVE_FUNC( StringToUint64 );
	RTTI_DEFINE_NATIVE_FUNC( StringToFloat );
	RTTI_DEFINE_NATIVE_FUNC( IsStringNumber );
}
