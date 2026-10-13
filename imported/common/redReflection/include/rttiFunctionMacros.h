#pragma once

#include "rttiFunction.h"
#include "rttiCommon.h"
#include "rttiRegistration.h"

namespace rtti
{
	class FunctionInitializer
	{
	public:
		static void CalcDataLayout( rtti::Function* function )
		{
			function->CalcDataLayout();
		}
	};

	// Base template function creator
	template < typename CLASS, bool IsScriptable = std::is_base_of< IScriptable, CLASS >::value >
	struct TFunctionCreator {};

	// Function creator specialized for IScriptable subclasses
	template < typename CLASS >
	struct TFunctionCreator< CLASS, true >
	{
		template < typename FUNC_TYPE >
		static rtti::NativeMemberFunction Create( rtti::ClassType* cls, const CName name, const CName familyName, FUNC_TYPE func, Uint32 flags = 0 )
		{
			return rtti::NativeMemberFunction( cls, name, familyName, static_cast< TNativeFunc >( func ), flags );
		}
	};

	// Function creator specialized for non-IScriptable subclasses
	template < typename CLASS >
	struct TFunctionCreator< CLASS, false >
	{
		template < typename FUNC_TYPE >
		static rtti::NativeFunctionCaller< CLASS > Create( rtti::ClassType* cls, const CName name, const CName familyName, FUNC_TYPE func, Uint32 flags = 0 )
		{
			return rtti::NativeFunctionCaller< CLASS >( cls, name, familyName, rtti::TFunctionCaller< CLASS >( func ), flags );
		}
	};
}

#define RTTI_INTERNAL_NATIVE_FUNCTION_2( name_, function_ ) \
	[registeredClass]{ \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeMemberFunction func( registeredClass, funcName, funcName, static_cast< TNativeFunc >( &tCurrClassType::function_ ) ); \
		registeredClass->AddFunction( &func ); \
	}()

#define RTTI_INTERNAL_NATIVE_FUNCTION_3( name_, function_, flags_ ) \
	[registeredClass]{ \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeMemberFunction func( registeredClass, funcName, funcName, static_cast< TNativeFunc >( &tCurrClassType::function_ ), flags_ ); \
		registeredClass->AddFunction( &func ); \
	}()

#define RTTI_NATIVE_FUNCTION( ... ) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_NATIVE_FUNCTION_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_INTERNAL_NATIVE_STATIC_FUNCTION_2( name_, function_ ) \
	[registeredClass]{ \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeMemberFunction func( registeredClass, funcName, funcName, static_cast< TNativeGlobalFunc >( &tCurrClassType::function_ ) ); \
		registeredClass->AddStaticFunction( &func ); \
	}()

#define RTTI_INTERNAL_NATIVE_STATIC_FUNCTION_3( name_, function_, flags_ ) \
	[registeredClass]{ \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeMemberFunction func( registeredClass, funcName, funcName, static_cast< TNativeGlobalFunc >( &tCurrClassType::function_ ), flags_ ); \
		registeredClass->AddStaticFunction( &func ); \
	}()

#define RTTI_NATIVE_STATIC_FUNCTION(...) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_NATIVE_STATIC_FUNCTION_, __VA_ARGS__)(__VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_INTERNAL_BEGIN_NATIVE_FUNCTION_2( name_, function_ ) { \
	static auto registeredFunction = rtti::TFunctionCreator< tCurrClassType >::Create( registeredClass, RED_NAME_CONSTEXPR( name_ ), RED_NAME_CONSTEXPR( name_ ), &tCurrClassType::function_ ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = false;

#define RTTI_INTERNAL_BEGIN_NATIVE_FUNCTION_3( name_, function_, flags_ ) { \
	static auto registeredFunction = rtti::TFunctionCreator< tCurrClassType >::Create( registeredClass, RED_NAME_CONSTEXPR( name_ ), RED_NAME_CONSTEXPR( name_ ), &tCurrClassType::function_, flags_ ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = false;

#define RTTI_BEGIN_NATIVE_FUNCTION(...) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_BEGIN_NATIVE_FUNCTION_, __VA_ARGS__)(__VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_END_NATIVE_FUNCTION() \
	rtti::FunctionParamBinder::AddParamToFunction( &registeredFunction, currFunctionParamBuilder ); \
	rtti::FunctionInitializer::CalcDataLayout( &registeredFunction ); \
	registeredClass->AddFunction( &registeredFunction ); \
}

#define RTTI_INTERNAL_BEGIN_NATIVE_STATIC_FUNCTION_2( name_, function_ ) { \
	const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
	static rtti::NativeMemberFunction registeredFunction( registeredClass, funcName, funcName, static_cast< TNativeGlobalFunc >( &tCurrClassType::function_ ) ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = true;

#define RTTI_INTERNAL_BEGIN_NATIVE_STATIC_FUNCTION_3( name_, function_, flags_ ) { \
	const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
	static rtti::NativeMemberFunction registeredFunction( registeredClass, funcName, funcName, static_cast< TNativeGlobalFunc >( &tCurrClassType::function_ ), flags_  ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = true;

#define RTTI_BEGIN_NATIVE_STATIC_FUNCTION( ... ) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_BEGIN_NATIVE_STATIC_FUNCTION_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_END_NATIVE_STATIC_FUNCTION() \
	rtti::FunctionParamBinder::AddParamToFunction( &registeredFunction, currFunctionParamBuilder ); \
	rtti::FunctionInitializer::CalcDataLayout( &registeredFunction ); \
	registeredClass->AddStaticFunction( &registeredFunction ); \
}

#define RTTI_INTERNAL_BEGIN_NATIVE_GLOBAL_FUNCTION_2( name_, function_ ) { \
	const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
	static rtti::NativeGlobalFunction registeredFunction( funcName, funcName, static_cast< TNativeGlobalFunc >( &function_ ) ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = false;

#define RTTI_INTERNAL_BEGIN_NATIVE_GLOBAL_FUNCTION_3( name_, function_, flags_ ) { \
	static rtti::NativeGlobalFunction registeredFunction( funcName, funcName, static_cast< TNativeGlobalFunc >( &function_ ), flags_ ); \
	rtti::FunctionParamBuilder* currFunctionParamBuilder = nullptr; \
	const Bool isStaticClassFunction = false;

#define RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( ... ) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_BEGIN_NATIVE_GLOBAL_FUNCTION_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_END_NATIVE_GLOBAL_FUNCTION() \
	rtti::FunctionParamBinder::AddParamToFunction( &registeredFunction, currFunctionParamBuilder ); \
	rtti::FunctionInitializer::CalcDataLayout( &registeredFunction ); \
	RTTIRegisterGlobalFunction( &registeredFunction ); \
}

#define RTTI_INTERNAL_NATIVE_GLOBAL_FUNCTION_2( name_, function_ ) { \
	RTTIRegistrator registrator( []() { \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeGlobalFunction func( funcName, funcName, static_cast< TNativeGlobalFunc >( &function_ ) ); \
		RTTIRegisterGlobalFunction( &func ); \
	} ); \
}

#define RTTI_INTERNAL_NATIVE_GLOBAL_FUNCTION_3( name_, function_, flags_ ) { \
	RTTIRegistrator registrator( []() { \
		const CName funcName = RED_NAME_CONSTEXPR( name_ ); \
		static rtti::NativeGlobalFunction func( funcName, funcName, static_cast< TNativeGlobalFunc >( &function_ ), flags_ ); \
		RTTIRegisterGlobalFunction( &func ); \
	} ); \
}

#define RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( ... ) \
	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RTTI_INTERNAL_NATIVE_GLOBAL_FUNCTION_, __VA_ARGS__)(__VA_ARGS__ ), RED_MEMORY_EMPTY() );

#define RTTI_FUNC_PARAM( name_, type_ ) \
	static_assert( !std::is_base_of< ISerializable, type_ >::value, "ISerializable function parameters not supported. Wrap your param in a THandle instead." ); \
	static_assert( !std::is_pointer< type_ >::value, "Pointer function parameters not supported"); \
	rtti::FunctionParamBinder::AddParamToFunction( &registeredFunction, currFunctionParamBuilder ); \
	currFunctionParamBuilder = RED_NEW( rtti::FunctionParamBuilder ) ( \
		&registeredFunction, \
		RED_NAME_CONSTEXPR( name_ ), \
		::GetTypeObject< type_ >() \
	); \
	(*currFunctionParamBuilder)

#define RTTI_REPLICATED_FUNCTION() \
	rep::IRTTIService::GetInstance().MakeFunctionReplicable( &registeredFunction, isStaticClassFunction );

#define RTTI_EXECUTION_TARGET( target_ ) \
	rep::IRTTIService::GetInstance().SetFunctionExecutionTarget( &registeredFunction, target_ );

#define RTTI_RELIABLE_FUNCTION() \
	rep::IRTTIService::GetInstance().SetFunctionReliable( &registeredFunction );