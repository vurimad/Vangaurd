/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "rttiSystem.h"

class IScriptable;

namespace rep
{
	class ReplicationContext;
}

namespace rtti
{
	class ClassType;
	class Function;
	class IType;

//////////////////////////////////////////////////////////////////////////
// Function call context

struct RED_REFLECTION_API FunctionContext : red::NonCopyable
{
	virtual ~FunctionContext();

	// Get pointer to the result buffer, if nullptr to data will be written
	virtual void* GetResultPtr() { return nullptr; }

	// Get result type
	virtual const rtti::IType* GetResultType() const { return nullptr; }

	// Finalize context based on the call result
	virtual void OnCalled( Bool success ) { RED_UNUSED( success ); }

	RED_INLINE void* GetContext() const { return m_object; }
	IScriptable* GetScriptableContext() const;
	Bool ValidateNativeContext( const rtti::Function* func ) const;

	rep::ReplicationContext* m_replicationContext;
	const void* m_userData;

protected:

	FunctionContext( IScriptable* scriptable, const void* userData );
	FunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData );

	// Context can be provided either by pointer to IScriptable or pointer to any (rtti) object and its class
	IScriptable* m_scriptable;
	void* m_object;
	const rtti::ClassType* m_objectClass;
};

//////////////////////////////////////////////////////////////////////////
// Scripted (only) function call context

struct ScriptedFunctionContext : public FunctionContext
{
	struct Setup
	{
		void* m_params;
		void* m_locals;
		Bool m_destroyParams;
	};

	virtual ~ScriptedFunctionContext() {};

	// Create params on stack (if needed)
	// Return structure which describes where specific data should came from
	virtual Setup InitializeParamsAndResult( const rtti::Function* func, void* stack ) = 0;

protected:

	ScriptedFunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData );
	ScriptedFunctionContext( IScriptable* scriptable, const void* userData );
};

//////////////////////////////////////////////////////////////////////////
// Scripted or native function call context

struct ScriptedOrNativeFunctionContext : public ScriptedFunctionContext
{
	static constexpr Uint32 c_maxCodeSize = 256;
	typedef Uint8 CodeBuffer[ c_maxCodeSize ];

	virtual ~ScriptedOrNativeFunctionContext() {};

	// Create scripted code for external native params
	virtual void CreateCodeForParams( CodeBuffer codeBuffer ) const = 0;

	RED_INLINE ScriptedFunctionContext& AsScriptedFunctionContext() { return reinterpret_cast< ScriptedFunctionContext& >( *this  ); }

protected:

	ScriptedOrNativeFunctionContext( void* object, const rtti::ClassType* objectClass, const void* userData );
	ScriptedOrNativeFunctionContext( IScriptable* scriptable, const void* userData );
};

//////////////////////////////////////////////////////////////////////////
// Array of rtti-based params

struct FunctionParamData
{
	const rtti::IType* m_type;
	const void* m_data;

	RED_INLINE FunctionParamData()
		: m_type( nullptr )
		, m_data( nullptr )
	{}

	template < typename T >
	RED_INLINE FunctionParamData( const T& data )
		: m_type( GetTypeObject< T >() )
		, m_data( &data )
	{}
};

struct FunctionReturnData
{
	const rtti::IType* m_type;
	void* m_data;

	RED_INLINE FunctionReturnData()
		: m_type( nullptr )
		, m_data( nullptr )
	{}

	template < typename T >
	RED_INLINE FunctionReturnData( T& data )
		: m_type( GetTypeObject< T >() )
		, m_data( &data )
	{}
};

struct RED_REFLECTION_API FunctionContext_RttiParams final : public ScriptedOrNativeFunctionContext
{
	FunctionContext_RttiParams( void* object, const rtti::ClassType* objectClass, const FunctionParamData* params, Uint32 paramsCount, FunctionReturnData* result, const void* userData );
	FunctionContext_RttiParams( IScriptable* scriptable, const FunctionParamData* params, Uint32 paramsCount, FunctionReturnData* result, const void* userData );

private:
	
	virtual Setup InitializeParamsAndResult( const rtti::Function* func, void* stack ) override;
	virtual void CreateCodeForParams( CodeBuffer codeBuffer ) const override;
	virtual void* GetResultPtr() override;
	virtual const rtti::IType* GetResultType() const override;

	const FunctionParamData* m_params;
	Uint32 m_paramsCount;
	FunctionReturnData* m_result;
};

//////////////////////////////////////////////////////////////////////////
// Array of red::String params

struct RED_REFLECTION_API FunctionContext_StringParams final : public ScriptedFunctionContext
{
	FunctionContext_StringParams( IScriptable* scriptable, const red::DynArray< red::String >* params, red::String* result, const void* userData );

private:

	virtual Setup InitializeParamsAndResult( const rtti::Function* func, void* stack ) override;
	virtual void* GetResultPtr() override;
	virtual const rtti::IType* GetResultType() const override;
	virtual void OnCalled( Bool success ) override;

	const red::DynArray< red::String >* m_params;
	red::String* m_result;
	const rtti::IType* m_resultType;
	red::DynArray< Uint8 > m_resultBuf{ red::PoolScript() };
};

//////////////////////////////////////////////////////////////////////////
// Context containing already created external params

struct RED_REFLECTION_API FunctionContext_ExternalParams final : public ScriptedFunctionContext
{
	FunctionContext_ExternalParams( IScriptable* scriptable, void* params, void* result, const rtti::IType* resultType, const void* userData );

private:

	virtual Setup InitializeParamsAndResult( const rtti::Function* func, void* stack ) override;
	virtual void* GetResultPtr() override { return m_result; }
	virtual const rtti::IType* GetResultType() const override { return m_resultType; }

	void* m_params;
	void* m_result;
	const rtti::IType* m_resultType;

};

} // rtti