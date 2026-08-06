/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiType.h"
#include "rttiFunction.h"
#include "rttiProperty.h"
#include "scriptableContextLock.h"
#include "rttiFunctionNameDecoration.h"

namespace rtti
{

#ifndef NO_SCRIPT_FUNCTION_CALL_VALIDATION

// #define IGNORE_CHECK_RESULT

	#ifdef IGNORE_CHECK_RESULT
		#define RETURN_ON_CHECK_FAILURE( check ) check
	#else
		#define RETURN_ON_CHECK_FAILURE( check ) if( !( check ) ) return false
	#endif

	/// These add a slight performance impact to each call
	#define CHECK_FUNCTION( context, function, numParameters )	RETURN_ON_CHECK_FAILURE( CheckFunction( context, function, numParameters ) )
	#define CHECK_RETURN( function, T )							RETURN_ON_CHECK_FAILURE( CheckFunctionReturnParameter( function, ::GetTypeObject< T >() ) )

#else
	#define CHECK_FUNCTION( context, function, numParameters )
	#define CHECK_RETURN( function, T )
#endif

//////////////////////////////////////////////////////////////////////////////////////////

/// Find function, uses cache
extern RED_REFLECTION_API Bool FindFunction( IScriptable*& context, CName functionName, const rtti::Function*& function );

/// Returns first found function from given family
extern RED_REFLECTION_API Bool FindFunctionFromFamily( IScriptable*& context, CName familyName, const rtti::Function*& function );

extern RED_REFLECTION_API Bool FindFunctionFromHash( IScriptable*& context, Uint64 hash, const rtti::Function*& function );

/// Check if passed function is valid global function
extern RED_REFLECTION_API Bool CheckFunction( IScriptable* context, const rtti::Function* function, Uint32 numParameters );

/// Check return parameter
extern RED_REFLECTION_API Bool CheckFunctionReturnParameter( const rtti::Function* function, const rtti::IType* returnType );

/// Check type of n-th function parameter
extern RED_REFLECTION_API Bool CheckFunctionParameter( const rtti::Function* function, Uint32 parameterIndex, const rtti::IType* parameterType, Bool silentCheck = false );

//////////////////////////////////////////////////////////////////////////////////////////

namespace utils
{

//////////////////////////////////////////////////////////////////////////
// Create array of FunctionParamData

RED_INLINE void CreateFunctionParamData( rtti::FunctionParamData*, Uint32 )
{
	// no params
}

template < typename ParamType, typename... Args >
RED_INLINE void CreateFunctionParamData( rtti::FunctionParamData* params, Uint32 paramIndex, const ParamType& data, const Args&... args )
{
	params[ paramIndex ] = rtti::FunctionParamData( data );
	CreateFunctionParamData( params, paramIndex + 1, args... );
}

template < typename... Args >
RED_INLINE void CreateFunctionParamsData( rtti::FunctionParamData* params, const Args&... args )
{
	CreateFunctionParamData( params, 0, args... );
}

//////////////////////////////////////////////////////////////////////////
// Validate parameters

RED_INLINE Bool ValidateFunctionParameter( const rtti::Function*, Uint32 )
{
	return true;
}

template < typename ParamType, typename... Args >
RED_INLINE Bool ValidateFunctionParameter( const rtti::Function* function, Uint32 paramIndex, const ParamType&, const Args&... args )
{
#ifdef NO_SCRIPT_FUNCTION_CALL_VALIDATION
	return true;
#else
	RETURN_ON_CHECK_FAILURE( CheckFunctionParameter( function, paramIndex, ::GetTypeObject< ParamType >() ) );
	return ValidateFunctionParameter( function, paramIndex + 1, args... );
#endif
}

template < typename... Args >
RED_INLINE Bool ValidateFunctionParameters( const rtti::Function* function, const Args&... args )
{
#ifdef NO_SCRIPT_FUNCTION_CALL_VALIDATION
	return true;
#else
	return ValidateFunctionParameter( function, 0, args... );
#endif
}

//////////////////////////////////////////////////////////////////////////
// Create parameters copy on the stack

RED_INLINE void AddParameterToStack( const rtti::Function*, void*, Uint32 )
{
	// no params
}

template < typename ParamType, typename... Args >
RED_INLINE void AddParameterToStack( const rtti::Function* function, void* stack, Uint32 paramIndex, const ParamType& data, const Args&... args )
{
	void* const ptr = red::OffsetPtr( stack, function->GetParameter( paramIndex )->GetDataOffset() );
	::new ( ptr ) ParamType( data );
	
	AddParameterToStack( function, stack, paramIndex + 1, args... );
}

template < typename... Args >
RED_INLINE void AddParametersToStack( const rtti::Function* function, void* stack, const Args&... args )
{
	AddParameterToStack( function, stack, 0, args... );
}

//////////////////////////////////////////////////////////////////////////
// Destroy parameters created on stack

RED_INLINE void DestroyStackParameter( const rtti::Function*, void*, Uint32 )
{
	// no params
}

template < typename ParamType, typename... Args >
RED_INLINE void DestroyStackParameter( const rtti::Function* function, void* stack, Uint32 paramIndex, const ParamType&, const Args&... args )
{
	ParamType* ptr = reinterpret_cast< ParamType* >( red::OffsetPtr( stack, function->GetParameter( paramIndex )->GetDataOffset() ) );
	ptr->~ParamType();
	
	DestroyStackParameter( function, stack, paramIndex + 1, args... );

}

template < typename... Args >
RED_INLINE void DestroyStackParameters( const rtti::Function* function, void* stack, const Args&... args )
{
	DestroyStackParameter( function, stack, 0, args... );
}

//////////////////////////////////////////////////////////////////////////
// Copy "out" values from the stack

RED_INLINE void GetStackOutValue( const rtti::Function*, void*, Uint32 )
{
	// end of params
}

template < typename ParamType, typename... Args >
RED_INLINE void GetStackOutValue( const rtti::Function* function, void* stack, Uint32 paramIndex, ParamType& data, Args&... args )
{
	ParamType* ptr = reinterpret_cast< ParamType* >( red::OffsetPtr( stack, function->GetParameter( paramIndex )->GetDataOffset() ) );
	data = *ptr;
	
	GetStackOutValue( function, stack, paramIndex + 1, args... );
}

template < typename... Args >
RED_INLINE void GetStackOutValues( const rtti::Function* function, void* stack, Args&... args )
{
	GetStackOutValue( function, stack, 0, args... );
}

} // utils

//////////////////////////////////////////////////////////////////////////////////////////

struct FunctionName
{
	CName	m_name;
	enum Format : Uint32
	{
		FNF_Decorated,
		FNF_Raw,
	}		m_format;

	RED_INLINE FunctionName( CName name, Format format  = FNF_Decorated )
		: m_name( name )
		, m_format( format )
	{}
};

struct RawFunctionName : public FunctionName
{
	RED_INLINE explicit RawFunctionName( CName name )
		: FunctionName( name, FNF_Raw )
	{}
};

namespace utils
{

	RED_INLINE void CombineFunctionParameterHash( Uint64 & hash ){}

	template< typename T, typename... Args >
	RED_INLINE void CombineFunctionParameterHash( Uint64 & hash, const T&, const Args&... args )
	{
		const CName paramTypeName = GetRttiSystem().NativeNameToScriptAlias( GetTypeObject< T >()->GetName() );
		hash = red::CombineHashes64( hash, paramTypeName.GetHash() );
		CombineFunctionParameterHash( hash, args... );
	}


	template< typename... Args >
	RED_INLINE Uint64 GetDecoratedFunctionHash( CName functionName, const Args&... args )
	{
		Uint64 hash = functionName.GetHash();
		CombineFunctionParameterHash( hash, args... );
		return hash;
	}

	template < typename... Args >
	RED_INLINE CName GetRealFunctionName( const FunctionName& fn, const Args&... args )
	{
		if(fn.m_format == FunctionName::FNF_Raw)
		{
			return fn.m_name;
		}

		return FunctionNameDecorator::CreateDecoratedName( DecoratorFunctionDescription( fn.m_name, args... ) );
	}

} // utils

//////////////////////////////////////////////////////////////////////////////////////////
// Call function:
// - CallFunction - "in" parameters
// - CallFunctionRet - "in" parameters, return ResultType
// - CallFunctionRef - "out" parameters
// - CallFunctionRefRet - "out" parameters, return ResultType

template < typename... Args  >
Bool CallFunction( IScriptable* context, const FunctionName& functionName, const Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );
		
		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, nullptr, nullptr, nullptr );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

template < typename... Args  >
Bool CallFunction_UserContext( IScriptable* context, const void* userContext, const FunctionName& functionName, const Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function ) 
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if ( foundFunction )
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, nullptr, nullptr, userContext );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

template < typename ReturnType, typename... Args  >
Bool CallFunctionRet( IScriptable* context, const FunctionName& functionName, ReturnType& result, const Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );
		CHECK_RETURN( function, ReturnType );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, &result, GetTypeObject< ReturnType >(), nullptr );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

template < typename ReturnType, typename... Args  >
Bool CallFunctionRet_UserContext( IScriptable* context, const void* userContext, const FunctionName& functionName, ReturnType& result, const Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );
		CHECK_RETURN( function, ReturnType );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, &result, GetTypeObject< ReturnType >(), userContext );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

template < typename... Args  >
Bool CallFunctionRef( IScriptable* context, const FunctionName& functionName, Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, nullptr, nullptr, nullptr );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::GetStackOutValues( function, stack, args... );
			utils::DestroyStackParameters( function, stack, args... );
			
			return res;
		}
	}
	return false;
}

template < typename... Args  >
Bool CallFunctionRef_UserContext( IScriptable* context, const void* userContext, const FunctionName& functionName, Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, nullptr, nullptr, userContext );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::GetStackOutValues( function, stack, args... );
			utils::DestroyStackParameters( function, stack, args... );

			return res;
		}
	}
	return false;
}

template < typename ReturnType, typename... Args  >
Bool CallFunctionRefRet( IScriptable* context, const FunctionName& functionName, ReturnType& result, Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );
		CHECK_RETURN( function, ReturnType );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, &result, ::GetTypeObject< ReturnType >(), nullptr );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::GetStackOutValues( function, stack, args... );
			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

template < typename ReturnType, typename... Args  >
Bool CallFunctionRefRet_UserContext( IScriptable* context, const void* userContext, const FunctionName& functionName, ReturnType& result, Args&... args )
{
	const rtti::Function* function = nullptr;
	const bool foundFunction = functionName.m_format == FunctionName::FNF_Raw
		? FindFunction( context, functionName.m_name, function )
		: FindFunctionFromHash( context, utils::GetDecoratedFunctionHash( functionName.m_name, args... ), function );

	if(foundFunction)
	{
		CHECK_FUNCTION( context, function, sizeof...( Args ) );
		CHECK_RETURN( function, ReturnType );

		if ( utils::ValidateFunctionParameters( function, args... ) )
		{
			void* stack = RED_ALLOCA( function->GetStackSize() );
			utils::AddParametersToStack( function, stack, args... );

			rtti::FunctionContext_ExternalParams callCtx( context, stack, &result, ::GetTypeObject< ReturnType >(), userContext );
			Bool res = false;
			{
				ScriptableContextLock lock( context, function );
				res = function->Call( callCtx );
			}

			utils::GetStackOutValues( function, stack, args... );
			utils::DestroyStackParameters( function, stack, args... );
			return res;
		}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////
// CallFunctionRaw

template < typename... Args >
Bool CallFunctionRaw( IScriptable* context, const rtti::Function* func, const Args&... args )
{
	constexpr Uint32 paramsNum = sizeof...( Args );
	// cause MSVC cannot create array of size 0
	constexpr Uint32 arraySize = paramsNum > 0 ? paramsNum : 1;

	rtti::FunctionParamData params[ arraySize ];
	utils::CreateFunctionParamsData( params, args... );

	rtti::FunctionContext_RttiParams callCtx( context, params, paramsNum, nullptr, nullptr );
	ScriptableContextLock lock( context, func );
	return func->Call( callCtx );
}

template < typename... Args >
Bool CallFunctionRaw_UserContext( IScriptable* context, const void* userContext, const rtti::Function* func, const Args&... args )
{
	constexpr Uint32 paramsNum = sizeof...( Args );
	// cause MSVC cannot create array of size 0
	constexpr Uint32 arraySize = paramsNum > 0 ? paramsNum : 1;

	rtti::FunctionParamData params[ arraySize ];
	utils::CreateFunctionParamsData( params, args... );

	rtti::FunctionContext_RttiParams callCtx( context, params, paramsNum, nullptr, userContext );
	ScriptableContextLock lock( context, func );
	return func->Call( callCtx );
}

template < typename ReturnType, typename... Args >
Bool CallFunctionRawRet( IScriptable* context, const rtti::Function* func, ReturnType& result, const Args&... args )
{
	constexpr Uint32 paramsNum = sizeof...( Args );
	// cause MSVC cannot create array of size 0
	constexpr Uint32 arraySize = paramsNum > 0 ? paramsNum : 1;

	rtti::FunctionParamData params[ arraySize ];
	utils::CreateFunctionParamsData( params, args... );
	rtti::FunctionReturnData returnData( result );

	rtti::FunctionContext_RttiParams callCtx( context, params, paramsNum, &returnData, nullptr );
	ScriptableContextLock lock( context, func );
	return func->Call( callCtx );
}

template < typename ReturnType, typename... Args >
Bool CallFunctionRawRet_UserContext( IScriptable* context, const void* userContext, const rtti::Function* func, ReturnType& result, const Args&... args )
{
	constexpr Uint32 paramsNum = sizeof...( Args );
	// cause MSVC cannot create array of size 0
	constexpr Uint32 arraySize = paramsNum > 0 ? paramsNum : 1;

	rtti::FunctionParamData params[ arraySize ];
	utils::CreateFunctionParamsData( params, args... );
	rtti::FunctionReturnData returnData( result );

	rtti::FunctionContext_RttiParams callCtx( context, params, paramsNum, &returnData, userContext );
	ScriptableContextLock lock( context, func );
	return func->Call( callCtx );
}

} // rtti
