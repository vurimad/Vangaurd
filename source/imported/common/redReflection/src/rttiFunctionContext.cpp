/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiFunctionContext.h"
#include "scriptable.h"
#include "scriptOpcodes.h"
#include "../../redContainers/include/dynArrayAccessor.h"

namespace rtti
{

FunctionContext::FunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData )
	: m_scriptable( nullptr )
	, m_object( object )
	, m_objectClass( objectClass )
	, m_replicationContext( nullptr )
	, m_userData( userData )
{}

FunctionContext::FunctionContext( IScriptable* scriptable, const void* userData )
	: m_scriptable( scriptable )
	, m_object( scriptable )
	, m_objectClass( ClassID< IScriptable >() )
	, m_replicationContext( nullptr )
	, m_userData( userData )
{}

FunctionContext::~FunctionContext()
{}

IScriptable* FunctionContext::GetScriptableContext() const
{
	return m_scriptable != nullptr ? m_scriptable : ( m_objectClass != nullptr && m_objectClass->IsA< IScriptable >() ? reinterpret_cast< IScriptable* >( m_object ) : nullptr );
}

Bool FunctionContext::ValidateNativeContext( const rtti::Function* func ) const
{
	return ( m_object != nullptr && m_objectClass->IsA( func->GetClass() ) );
}

//////////////////////////////////////////////////////////////////////////

ScriptedFunctionContext::ScriptedFunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData )
	: FunctionContext( object, objectClass, userData )
{}

ScriptedFunctionContext::ScriptedFunctionContext( IScriptable* scriptable, const void* userData )
	: FunctionContext( scriptable, userData )
{}

//////////////////////////////////////////////////////////////////////////

ScriptedOrNativeFunctionContext::ScriptedOrNativeFunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData )
	: ScriptedFunctionContext( object, objectClass, userData )
{}

ScriptedOrNativeFunctionContext::ScriptedOrNativeFunctionContext( IScriptable* scriptable, const void* userData )
	: ScriptedFunctionContext( scriptable, userData )
{}

//////////////////////////////////////////////////////////////////////////

FunctionContext_RttiParams::FunctionContext_RttiParams( void* object, const rtti::ClassType* objectClass, const FunctionParamData* params, Uint32 paramsCount,
														FunctionReturnData* result,	const void* userData )
	: ScriptedOrNativeFunctionContext( object, objectClass, userData )
	, m_params( params )
	, m_paramsCount( paramsCount )
	, m_result( result )
{
	RED_FATAL_ASSERT( paramsCount == 0 || params != nullptr, "No params specified" );
}

FunctionContext_RttiParams::FunctionContext_RttiParams( IScriptable* scriptable, const FunctionParamData* params, Uint32 paramsCount,
														FunctionReturnData* result, const void* userData )
	: ScriptedOrNativeFunctionContext( scriptable, userData )
	, m_params( params )
	, m_paramsCount( paramsCount )
	, m_result( result )
{
	RED_FATAL_ASSERT( paramsCount == 0 || params != nullptr, "No params specified" );
}

ScriptedFunctionContext::Setup FunctionContext_RttiParams::InitializeParamsAndResult( const rtti::Function* func, void* stack )
{
	red::Memset( stack, 0, func->GetStackSize() );

	// Evaluate function params
	for ( Uint32 i = 0, count = func->GetNumParameters(); i < count; i++ )
	{
		const Property* prop = func->GetParameter( i );

		// Construct the data
		void* propData = prop->GetOffsetPtr( stack );

		const IType * propType = prop->GetType();
		propType->Construct( propData );
		if(propType->GetType() == RT_Array)
		{
			red::DynArrayAccessor & array = red::DynArrayAccessor::GetRef( propData );
			array.SetPool( red::PoolScript() );
		}

		// Copy param
		if ( i < m_paramsCount && m_params[ i ].m_data != nullptr )
		{
			RED_FATAL_ASSERT( propType == m_params[ i ].m_type, "Incompatible type for call argument '%hs' (expected '%hs', given '%hs')",
				prop->GetName().AsChar(), propType->GetName().AsChar(), m_params[ i ].m_type->GetName().AsChar() );

			propType->Copy( propData, m_params[ i ].m_data );
		}
	}

	// Validate return value
	if ( m_result != nullptr && m_result->m_data != nullptr )
	{
		RED_FATAL_ASSERT( func->GetReturnValue()->GetType() == m_result->m_type, "Incompatible type for return value (expected '%hs', given '%hs')",
			func->GetReturnValue()->GetType()->GetName().AsChar(), m_result->m_type->GetName().AsChar() );
	}

	Setup setup;
	setup.m_params = stack; // both params and locals are stored on provided stack
	setup.m_locals = stack;
	setup.m_destroyParams = true; // params were constructed, so they need to be destroyed after execution	
	return setup;
}

void FunctionContext_RttiParams::CreateCodeForParams( CodeBuffer codeBuffer ) const
{
	// This is quite risky, as we can create opcodes only for specified number of parameters.
	// We DON'T KNOW how many parameters native functions needs, so in case there's more or less than we have, we may expect fatal assert or crash.

	Uint32 codeIndex = 0;

	for ( Uint32 i = 0; i < m_paramsCount; ++i )
	{
		const Uint32 newCodeSize = sizeof( void* ) * 2 + 1; // two pointers and 8-bit opcode
		RED_FATAL_ASSERT( codeIndex + newCodeSize < c_maxCodeSize );

		// write opcode
		codeBuffer[ codeIndex++ ] = static_cast< Uint8 >( OP_ExternalVar );
		// write type pointer
		*reinterpret_cast< void** >( &codeBuffer[ codeIndex ] ) = const_cast< rtti::IType* >( m_params[ i ].m_type );
		codeIndex += sizeof( void* );
		// write data pointer
		*reinterpret_cast< void** >( &codeBuffer[ codeIndex ] ) = const_cast< void* >( m_params[ i ].m_data );
		codeIndex += sizeof( void* );
	}

	// end of parameters marker
	codeBuffer[ codeIndex ] = static_cast< Uint8 >( OP_ParamEnd );
}

void* FunctionContext_RttiParams::GetResultPtr()
{
	return m_result != nullptr ? m_result->m_data : nullptr;
}

const rtti::IType* FunctionContext_RttiParams::GetResultType() const
{
	return m_result != nullptr ? m_result->m_type : nullptr;
}

//////////////////////////////////////////////////////////////////////////

FunctionContext_StringParams::FunctionContext_StringParams( IScriptable* scriptable, const red::DynArray< red::String >* params, red::String* result, const void* userData )
	: ScriptedFunctionContext( scriptable, userData )
	, m_params( params )
	, m_result( result )
	, m_resultType( nullptr )
{
	RED_FATAL_ASSERT( params != nullptr, "No params specified" );
}

ScriptedFunctionContext::Setup FunctionContext_StringParams::InitializeParamsAndResult( const rtti::Function* func, void* stack )
{
	red::Memset( stack, 0, func->GetStackSize() );

	// Evaluate function params
	for ( Uint32 i = 0, count = func->GetNumParameters(); i < count; i++ )
	{
		const Property* prop = func->GetParameter( i );
		

		// Construct the data
		void* propData = prop->GetOffsetPtr( stack );

		const IType * propType = prop->GetType();
		propType->Construct( propData );
		if( propType->GetType() == RT_Array )
		{
			red::DynArrayAccessor & array = red::DynArrayAccessor::GetRef( propData );
			array.SetPool( red::PoolScript() );
		}

		// Evaluate from string
		if ( i < m_params->Size() )
		{
			propType->FromString( propData, ( *m_params )[ i ] );
		}
	}

	// Prepare buffer for result value
	if ( m_result != nullptr )
	{
		const rtti::Property* returnProperty = func->GetReturnValue();
		if ( returnProperty != nullptr )
		{
			m_resultType = returnProperty->GetType();
			m_resultBuf.Resize( m_resultType->GetSize() );
		}
	}

	Setup setup;
	setup.m_params = stack; // both params and locals are stored on provided stack
	setup.m_locals = stack;
	setup.m_destroyParams = true; // params were constructed, so they need to be destroyed after execution	
	return setup;
}

void* FunctionContext_StringParams::GetResultPtr()
{
	return m_resultBuf.Data();
}

const rtti::IType* FunctionContext_StringParams::GetResultType() const
{
	return m_resultType;
}

void FunctionContext_StringParams::OnCalled( Bool success )
{
	if ( !m_resultBuf.Empty() )
	{
		if ( success )
		{
			m_resultType->ToString( m_resultBuf.Data(), *m_result );
		}
		m_resultType->Destruct( m_resultBuf.Data() );
	}
}

//////////////////////////////////////////////////////////////////////////

FunctionContext_ExternalParams::FunctionContext_ExternalParams( IScriptable* scriptable, void* params, void* result, const rtti::IType* resultType, const void* userData )
	: ScriptedFunctionContext( scriptable, userData )
	, m_params( params )
	, m_result( result )
	, m_resultType( resultType )
{}

ScriptedFunctionContext::Setup FunctionContext_ExternalParams::InitializeParamsAndResult( const rtti::Function* func, void* stack )
{
	red::Memset( stack, 0, func->GetStackSize() );

	Setup setup;
	setup.m_params = m_params; // use params stored in external buffer
	setup.m_locals = stack; // locals are stored on provided stack
	setup.m_destroyParams = false; // we didn't create the param, so don't need to destroy them
	return setup;
}

} // rtti