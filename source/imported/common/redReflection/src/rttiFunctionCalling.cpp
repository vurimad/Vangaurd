/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiFunctionCalling.h"
#include "rttiUtils.h"
#include "scriptable.h"
#include "rttiSystem.h"

#define SCRIPT_API_ERROR( message, ... )  RED_LOG_ERROR( "ScriptApi: " message, ##__VA_ARGS__ ); RED_HALT( message, ##__VA_ARGS__ )

namespace rtti
{

Bool CheckFunction( IScriptable* context, const rtti::Function* function, Uint32 numParameters )
{
	// No function...
	if ( !function )
	{
		SCRIPT_API_ERROR( "Missing function to call" );
		return false;
	}

	// Non static functions require context
	if ( !function->IsStatic() )
	{
		if ( !context )
		{
			SCRIPT_API_ERROR( "Non static function '%hs' requires a context (IScriptable). None given.", function->GetName().AsChar() );
			return false;
		}
	}

	// Check if has parameters
	if ( function->GetNumParameters() < numParameters )
	{
		SCRIPT_API_ERROR( "Trying to call function '%hs' with too many parameters (%i, expects %i)", function->GetName().AsChar(), numParameters, function->GetNumParameters() );
		return false;
	}

	// Check if has parameters
	if ( function->GetNumParameters() > numParameters )
	{
		// Check to see if the missing parameters are optional
		if( !( function->GetParameter( numParameters )->GetFlags() & PF_FuncOptionaParam ) )
		{
			SCRIPT_API_ERROR( "Trying to call function '%hs' with too few parameters (%i, expects %i)", function->GetName().AsChar(), numParameters, function->GetNumParameters() );
			return false;
		}
	}

	// Seems ok
	return true;
}

Bool CheckTypeForInput( const rtti::Function* function, const rtti::Property* scriptProperty, const rtti::IType* nativeType, Bool silentCheck = false )
{
	// note the cast order for INPUT to scripts: "c++ type" -> "property type (in scripts)"
	if ( rtti::CanCast( nativeType, scriptProperty->GetType() ) )
	{
		return true;
	}
	
	RED_UNUSED( function );
	if ( ! silentCheck )
	{
		// Error
		SCRIPT_API_ERROR
			(
			"Input parameter '%hs' in function '%hs' is of type '%hs', expected '%hs'",
			scriptProperty->GetName().AsChar(),
			function->GetName().AsChar(),
			nativeType->GetName().AsChar(),
			scriptProperty->GetType()->GetName().AsChar()
			);
	}

	return false;
}

Bool CheckTypeForOutput( const rtti::Function* function, const rtti::Property* scriptProperty, const rtti::IType* nativeType, Bool silentCheck = false )
{
	// note the cast order for OUTPUT from scripts: "property type (in scripts)" -> "c++ type"
	if ( rtti::CanCast( scriptProperty->GetType(), nativeType ) )
	{
		return true;
	}
	
	// Error
	RED_UNUSED( function );

	if ( ! silentCheck)
	{
		SCRIPT_API_ERROR
			(
			"Output parameter '%hs' in function '%hs' is of type '%hs', expected '%hs'",
			scriptProperty->GetName().AsChar(),
			function->GetName().AsChar(),
			nativeType->GetName().AsChar(),
			scriptProperty->GetType()->GetName().AsChar()
			);
	}

	return false;
}

Bool CheckFunctionReturnParameter( const rtti::Function* function, const rtti::IType* returnType )
{
	if ( !function->GetReturnValue() )
	{
		SCRIPT_API_ERROR( "Function '%hs' does not return any value.", function->GetName().AsChar() );
		return false;
	}

	CheckTypeForOutput( function, function->GetReturnValue(), returnType );

	// Seems ok
	return true;
}

Bool CheckFunctionParameter( const rtti::Function* function, Uint32 parameterIndex, const rtti::IType* returnType, Bool silentCheck /*= false */ )
{
	// Check if has parameters
	if ( function->GetNumParameters() < parameterIndex )
	{
		if ( ! silentCheck )
		{
			SCRIPT_API_ERROR( "Trying to add too many parameters to function '%hs', paramIndex=%u, number of function parameters=%u", function->GetName().AsChar(), parameterIndex, 
				static_cast<Uint32>(function->GetNumParameters()) );
		}
		return false;
	}

	// Check type of parameter
	const rtti::Property* param = function->GetParameter( parameterIndex );
	if ( !CheckTypeForInput( function, param, returnType, silentCheck ) )
	{
		return false;
	}

	// Additional checks for bi-directional or output parameters
	if ( param->GetType()->GetSize() !=  returnType->GetSize() )
	{
		if ( ! silentCheck )
		{
			// Error
			SCRIPT_API_ERROR
				(
				"Input parameter '%hs' in function '%hs' has size %i bytes, expected %i bytes",
				param->GetName().AsChar(),
				function->GetName().AsChar(),
				returnType->GetSize(),
				param->GetType()->GetSize()
				);
		}
		return false;
	}

	if ( ( param->GetFlags() & PF_FuncOutParam ) != 0 )
	{
		if ( !CheckTypeForOutput( function, param, returnType, silentCheck ) )
		{
			return false;
		}
	}

	// Seems ok
	return true;
}

Bool FindFunction( IScriptable*& context, CName functionName, const rtti::Function*& function )
{
	// Use global function
	if ( !context )
	{
		function = GetRttiSystem().FindGlobalFunction( functionName );
		return function != nullptr;
	}

	// Find function inside the context object
	function = context->FindFunction( functionName );
	return function != nullptr;
}

Bool FindFunctionFromHash( IScriptable*& context, Uint64 hash, const rtti::Function*& function )
{
	if(!context)
	{
		function = GetRttiSystem().FindGlobalFunction( hash );
		return function != nullptr;
	}

	function = context->FindFunctionByHash( hash );
	return function != nullptr;
}

Bool FindFunctionFromFamily( IScriptable*& context, CName familyName, const rtti::Function*& function )
{
	struct Collector : public rtti::IFunctionCollector
	{
		CName m_name;
		const rtti::Function* m_function;

		RED_INLINE Collector( CName name )
			: m_name( name )
			, m_function( nullptr )
		{}
		virtual Bool operator()( const Function* f ) override
		{
			if ( f->GetName() == m_name )
			{
				m_function = f;
				return false;
			}
			return true;
		}
	};

	Collector collector( familyName );
	
	// Use global functions
	if ( !context )
	{
		GetRttiSystem().EnumGlobalFunctionsFromFamily( collector );
	}
	else
	{
		context->EnumFunctionsFromFamily( context, collector );
	}
	function = collector.m_function;
	return function != nullptr;
}

} // rtti