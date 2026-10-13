/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptingSystem.h"
#include "scriptStackFrame.h"
#include "scriptOpcodesList.h"
#include "mathUtils.h"
#include "rttiRegistration.h"
#include "rttiFunction.h"

namespace Casting 
{
	/// default cast implementation
	template< typename SrcType, typename DestType >
	class CastOp
	{
	public:
		static void Cast( CScriptStackFrame& stack, const SrcType& src, DestType& ret )
		{
			ret = (DestType) src;
		}
	};

	template< typename SrcType >
	class CastOp< SrcType, Bool >
	{
	public:
		static void Cast( CScriptStackFrame& stack, const SrcType& src, Bool& ret )
		{
			ret = !!src;
		}
	};
	

	/// generic script wrapper for cast function
	template< typename SrcType, typename DestType >
	class CastFunc
	{
	public:
		static void ScriptFunc( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
		{
			SrcType value = 0;
			stack.Step( context, &value, GetTypeObject< SrcType >() );
			const Uint8 code = *stack.m_code++;
			
			RED_FATAL_ASSERT( code == OP_ParamEnd, "Invalid code stream" );
			if ( result ) 
			{
				CastOp< SrcType, DestType >::Cast( stack, value, *(DestType*) result );			
			}
		}
	};
}

#define RTTI_REGISTER_NATIVE_CAST( SrcType, DestType )																										\
{																																							\
	RTTIRegistrator registrator( []() {																														\
		const CName funcName = RED_NAME_CONSTEXPR( "Cast;" RED_EXPAND_AND_STRINGIFY( SrcType ) ";" RED_EXPAND_AND_STRINGIFY( DestType ) );										\
		rtti::Function* func = RED_NEW( rtti::NativeGlobalFunction )( funcName, funcName, static_cast< TNativeGlobalFunc >( &Casting::CastFunc<SrcType,DestType>::ScriptFunc ) );		\
		RTTIRegisterGlobalFunction( func );																										\
	} );																																					\
}

void RegisterCoreScriptCastFunctions()
{
	RTTI_REGISTER_NATIVE_CAST( Uint8, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Bool );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Float );
	RTTI_REGISTER_NATIVE_CAST( Uint8, Double );

	RTTI_REGISTER_NATIVE_CAST( Uint16, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Bool );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Float );
	RTTI_REGISTER_NATIVE_CAST( Uint16, Double );

	RTTI_REGISTER_NATIVE_CAST( Uint32, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Bool );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Float );
	RTTI_REGISTER_NATIVE_CAST( Uint32, Double );

	RTTI_REGISTER_NATIVE_CAST( Uint64, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Bool );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Float );
	RTTI_REGISTER_NATIVE_CAST( Uint64, Double );

	//--

	RTTI_REGISTER_NATIVE_CAST( Int8, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Int8, Bool );
	RTTI_REGISTER_NATIVE_CAST( Int8, Float );
	RTTI_REGISTER_NATIVE_CAST( Int8, Double );

	RTTI_REGISTER_NATIVE_CAST( Int16, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Int16, Bool );
	RTTI_REGISTER_NATIVE_CAST( Int16, Float );
	RTTI_REGISTER_NATIVE_CAST( Int16, Double );

	RTTI_REGISTER_NATIVE_CAST( Int32, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Int32, Bool );
	RTTI_REGISTER_NATIVE_CAST( Int32, Float );
	RTTI_REGISTER_NATIVE_CAST( Int32, Double );

	RTTI_REGISTER_NATIVE_CAST( Int64, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Int64, Bool );
	RTTI_REGISTER_NATIVE_CAST( Int64, Float );
	RTTI_REGISTER_NATIVE_CAST( Int64, Double );

	//--

	RTTI_REGISTER_NATIVE_CAST( Float, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Float, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Float, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Float, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Float, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Float, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Float, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Float, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Float, Bool );
	RTTI_REGISTER_NATIVE_CAST( Float, Double );

	RTTI_REGISTER_NATIVE_CAST( Double, Int8 );
	RTTI_REGISTER_NATIVE_CAST( Double, Int16 );
	RTTI_REGISTER_NATIVE_CAST( Double, Int32 );
	RTTI_REGISTER_NATIVE_CAST( Double, Int64 );
	RTTI_REGISTER_NATIVE_CAST( Double, Uint8 );
	RTTI_REGISTER_NATIVE_CAST( Double, Uint16 );
	RTTI_REGISTER_NATIVE_CAST( Double, Uint32 );
	RTTI_REGISTER_NATIVE_CAST( Double, Uint64 );
	RTTI_REGISTER_NATIVE_CAST( Double, Bool );
	RTTI_REGISTER_NATIVE_CAST( Double, Float );
}