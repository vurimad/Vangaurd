/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"

#include "scriptOpcodes.h"

#include "scriptable.h"
#include "scriptingSystem.h"
#include "scriptStackFrame.h"
#include "scriptLog.h"
#include "scriptOpcodesUtils.h"
#include "scriptArrayFunctions.h"
#include "scriptBreakpointCondition.h"
#include "rttiPointerTypesImpl.h"
#include "variant.h"
#include "rttiEnum.h"
#include "rttiArrayTypesImpl.h"
#include "scriptStackFrame.h"
#include "scriptableThreadSafetyMonitor.h"
#include "scriptingSystemImpl.h"
#include "resourceReferenceScriptToken.h"

using red::DynArray;
using red::HashMap;

#ifdef USE_PROFILER

namespace
{
	void StartProfile( CScriptStackFrame& stack, const red::AnsiChar* scopeName, red::InstrumentationObject* instrumentationObj )
	{
		RED_FATAL_ASSERT( !stack.m_perfData.instrObj );
		stack.m_perfData.instrObj = instrumentationObj;

		if ( instrumentationObj )
		{
			EProfilerBlockChannel prevChannel;
			if ( instrumentationObj->m_forceChannel != PBC_NONE )
			{
				prevChannel = red::SwapThreadLocalPerfChannels( instrumentationObj->m_forceChannel );
				instrumentationObj->m_prevChannel = prevChannel;
			}

			gProfilers.StartBlock( instrumentationObj, scopeName );
		}
	}
}

#endif

//------------------------------------------------------------------------------

const AnsiChar* GetOpcodeName( const EScriptOpcode op )
{
	switch ( op )
	{
#define OPCODE(x) case OP_##x: return #x;
#include "scriptOpcodesList.h"
#undef OPCODE
	}

	return "Unknown";
}

//------------------------------------------------------------------------------

void OpNop( IScriptable*, CScriptStackFrame& stack, void*, rtti::IType* )
{
}

void OpNull( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_OBJECT( nullptr );
}

void OpWeakNull( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_WEAK_OBJECT( nullptr );
}

void OpIntOne( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_INT( 1 );
}

void OpIntZero( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_INT( 0 );
}

void OpIntConst1( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Int8 >() );
}

void OpIntConst2( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Int16 >() );
}

void OpIntConst4( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Int32 >() );
}

void OpIntConst8( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Int64 >() );
}

void OpUintConst1( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Uint8 >() );
}

void OpUintConst2( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Uint16 >() );
}

void OpUintConst4( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Uint32 >() );
}

void OpUintConst8( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Uint64 >() );
}

void OpFloatConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Float >() );
}

void OpDoubleConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_AUTO( stack.Read< Double >() );
}

void OpStringConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const Uint32 length = stack.Read< Uint32 >();

	if ( result )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, script::GetRTTIType< String >() ), "Invalid result type" );
		String* value = reinterpret_cast<String*>( result );
		if ( length > 0 )
		{
			// prepare string buffer
			value->Resize( length );
			(*value)[ length ] = 0;

			// copy string chars
			red::Memcpy( &(*value)[0], stack.m_code, sizeof(char) * length );
		}
		else
		{
			// empty string
			value->Clear();
		}
	}

	// go to the end of the string data
	stack.m_code += sizeof(char) * length;
}

void OpNameConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	CName value = stack.Read< CName >();
	RETURN_NAME( value );
}

void OpTweakDBIDConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	TweakDBID id = stack.Read< TweakDBID >();
	RETURN_AUTO( id );
}

void OpResRefConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	red::ResourceReferenceScriptToken token = stack.Read< red::ResourceReferenceScriptToken >();
	RETURN_STRUCT( red::ResourceReferenceScriptToken, token );
}

void OpEnumConst( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the enum type
	const auto* enumType = stack.Read< const rtti::EnumType* >();
	RED_FATAL_ASSERT( enumType != nullptr, "Expecting enum type" );

	// Read resolved value
	// TODO: make this smaller!
	const Int64 value = stack.Read< Int64 >();
	if ( result )
	{
		switch ( enumType->GetSize() )
		{
			case 1: 
				*(Int8*) result = (Int8) value;
				break;

			case 2: 
				*(Int16*) result = (Int16) value;
				break;

			case 4: 
				*(Int32*) result = (Int32) value;
				break;

			case 8: 
				*(Int64*) result = value;
				break;
		}
	}
}

void OpBoolTrue( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_BOOL( true );
}

void OpBoolFalse( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_BOOL( false );
}

void OpBreakpoint( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read the line
	Uint16 line = stack.Read< Uint16 >();
	Uint32 position = stack.Read< Uint32 >();
	Uint16 column = stack.Read< Uint16 >();
	Uint16 length = stack.Read< Uint16 >();

	// Read the status
	Uint8 breakpointEnabled = stack.Read< Uint8 >();

	// See if we have a condition set on the breakpoint
	script::breakpoint::Condition* condition = stack.Read< script::breakpoint::Condition* >();

#ifndef NO_SCRIPT_DEBUG

	stack.m_debugData = CScriptStackFrame::DebugData( position, line, column, length );

	script::BreakpointType bpType = script::BreakpointType::Step;

	if( condition )
	{
		// If there is a condition, then the breakpoint must also be set
		RED_FATAL_ASSERT( breakpointEnabled, "Condition set on a disabled script breakpoint" );

		// If the condition fails, succeeds, it's a breakpoint if it doesn't the scripting system needs still needs to know about the breakpoint
		bpType = condition->Evaluate( stack ) ? script::BreakpointType::Breakpoint : script::BreakpointType::BreakpointConditionFailed;
		for ( CScriptStackFrame* currentStackFrame = &stack; currentStackFrame != nullptr; currentStackFrame = currentStackFrame->GetParent() )
		{
			currentStackFrame->m_isDebugging = true;
		}
	}
	else if( breakpointEnabled )
	{
		bpType = script::BreakpointType::Breakpoint;
		for ( CScriptStackFrame* currentStackFrame = &stack; currentStackFrame != nullptr; currentStackFrame = currentStackFrame->GetParent() )
		{
			currentStackFrame->m_isDebugging = true;
		}
	}

	IScriptingSystem::GetInstance().DebugBreakpoint( bpType, context, stack );

#endif

	// Step to the rest of the code
	stack.Step( context, result, resultType );
}

void OpStartProfile( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	static script::ProfilerMode profilerMode = CScriptingSystem::GetInstance().GetProfilerMode();

	const Uint32 size = stack.Read< Uint32 >();
	const char* scopeName = reinterpret_cast< const char* >( stack.m_code );
	stack.m_code += size;

	red::InstrumentationObject* instrumentationObj = stack.Read< red::InstrumentationObject* >();
	const Bool markedForProfile = stack.Read< Int8 >();

#if defined( USE_PROFILER )

	switch ( profilerMode )
	{
	case script::ProfilerMode_EntryFunctions:
		if ( !stack.GetParent() )
		{
			StartProfile( stack, scopeName, instrumentationObj);
		}
		break;

	case script::ProfilerMode_MarkedFunctions:
		if ( markedForProfile )
		{
			StartProfile( stack, scopeName, instrumentationObj );
		}
		break;

	case script::ProfilerMode_All:
		StartProfile( stack, scopeName, instrumentationObj );
		break;

	case script::ProfilerMode_Off:
		break;

	default:
		RED_FATAL_ASSERT( "Unknown profiling mode" );
		break;
	}

#endif
}

void OpVirtualFunc( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RED_FATAL_ASSERT( context != nullptr, "Expecting object context" );

	// Calculate the address to jump in case state entry failed
	Uint16 skipSize = stack.Read< Uint16 >();
	const Uint8* codeSkip = stack.m_code + skipSize;

	// Get line
	const Uint32 scriptLine = stack.Read< Uint16 >();

#ifndef NO_SCRIPT_DEBUG 

	stack.m_debugData = CScriptStackFrame::DebugData( 0, scriptLine, 0, 0 );

#endif
	// Get the function name 
	CName funcName = stack.Read<CName>();

	// Find function to call
	const rtti::Function* functionToCall = context->FindFunction( funcName );
	if ( !functionToCall )
	{
		SCRIPT_RUNTIME_ERROR( stack, "Missing virtual function '%s', line %d, context '%s'", funcName.AsChar(), scriptLine, context->GetClass()->GetName().AsChar() );
		stack.m_code = codeSkip;
		return;
	}

	// Call the function
	{
		ScriptableContextLock lock( context, functionToCall );
		if ( !functionToCall->Call_ScriptsSystem( context, stack, result, resultType ) )
		{
			stack.m_code = codeSkip;
			return;
		}
	}

#ifndef NO_SCRIPT_DEBUG 
	// Returned from function
	IScriptingSystem::GetInstance().DebugBreakpoint( script::BreakpointType::FunctionExit, context, stack );
#endif
}

void OpFinalFunc( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Calculate the address to jump in case state entry failed
	Uint16 skipSize = stack.Read< Uint16 >();
	const Uint8* codeSkip = stack.m_code + skipSize; 

	// Get line
	const Uint32 scriptLine = stack.Read< Uint16 >();

#ifndef NO_SCRIPT_DEBUG 

	stack.m_debugData = CScriptStackFrame::DebugData( 0, scriptLine, 0, 0 );

#endif

	// Get the function to call
	const rtti::Function* functionToCall = stack.Read< const rtti::Function* >();
	RED_FATAL_ASSERT( functionToCall != nullptr, "Expecting function to call" );

	// Call the function
	{
		ScriptableContextLock lock( context, functionToCall );
		if ( !functionToCall->Call_ScriptsSystem( context, stack, result, resultType ) )
		{
			stack.m_code = codeSkip;
			return;
		}
	}
}

void OpReturn( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Calculate result
	stack.Step( stack.GetContext(), result, resultType );
}

void OpLocalVar( IScriptable*, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read property
	const auto* prop = stack.Read< const rtti::Property* >();
	RED_FATAL_ASSERT( prop != nullptr, "Invalid property access" );
	RED_FATAL_ASSERT( prop->IsFuncLocal(), "Expecting function local variable" );

	// Setup property reference
	void* propData = prop->GetOffsetPtr( stack.m_locals );
	stack.m_lValuePtr = propData;
	stack.m_lValueType = prop->GetType();
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	if ( stack.m_canElideCopy && stack.m_lValueType && stack.m_lValueType->GetType() == ERTTITypeType::RT_ScriptReference )
	{
		stack.m_canElideCopy = false;
	}

	// Copy property value if requested
	if ( result && !stack.m_canElideCopy )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( prop->GetType(), resultType ), "Invalid property type" );
		const rtti::IType* type = prop->GetType();
		type->Copy( result, propData );
	}
}

void OpParamVar( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Read property
	const auto* prop = stack.Read< const rtti::Property* >();
	RED_FATAL_ASSERT( prop != nullptr, "Invalid property access" );
	RED_FATAL_ASSERT( prop->IsFuncParam(), "Expecting function parameter" );

	// Setup property reference
	void* propData = prop->GetOffsetPtr( stack.m_params );
	stack.m_lValuePtr = propData;
	stack.m_lValueType = prop->GetType();
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	if ( stack.m_canElideCopy && stack.m_lValueType && stack.m_lValueType->GetType() == ERTTITypeType::RT_ScriptReference )
	{
		stack.m_canElideCopy = false;
	}

	// Copy property value if requested
	if ( result && !stack.m_canElideCopy )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( prop->GetType(), resultType ), "Invalid property type" );
		const rtti::IType* type = prop->GetType();
		type->Copy( result, propData );
	}
}

void OpObjectVar( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	RED_FATAL_ASSERT( context != nullptr, "Expecting class context" );

	// Read property
	const auto* prop = stack.Read< const rtti::Property* >();
	RED_FATAL_ASSERT( prop != nullptr, "Invalid property access" );
	RED_FATAL_ASSERT( !prop->IsInFunction(), "Expecting class/struct property" );

	// Setup property reference
	void* propData = prop->GetOffsetPtr( context );
	stack.m_lValuePtr = propData;
	stack.m_lValueType = prop->GetType();
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = prop;
	stack.m_lValuePropertyOwner = context;
#endif

	if ( stack.m_canElideCopy && stack.m_lValueType && stack.m_lValueType->GetType() == ERTTITypeType::RT_ScriptReference )
	{
		stack.m_canElideCopy = false;
	}

	// Copy property value if requested
	if ( result && !stack.m_canElideCopy )
	{
		RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();
		RED_FATAL_ASSERT( Helper::ValidateResultType( prop->GetType(), resultType ), "Invalid property type" );
		const rtti::IType* type = prop->GetType();
		type->Copy( result, propData );
	}
}

void OpExternalVar( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Read property
	const auto* propType = stack.Read< const rtti::IType* >();
	auto* propData = stack.Read< void* >();

	// Setup property reference
	stack.m_lValuePtr = propData;
	stack.m_lValueType = propType;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	if ( stack.m_canElideCopy && stack.m_lValueType && stack.m_lValueType->GetType() == ERTTITypeType::RT_ScriptReference )
	{
		stack.m_canElideCopy = false;
	}

	// Copy property value if requested
	if ( result && !stack.m_canElideCopy )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( propType, resultType ), "Invalid property type" );
		propType->Copy(result, propData);
	}
}

void OpContext( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Calculate the address to jump in case context will be invalid
	Uint16 skipSize = stack.Read< Uint16 >();
	const Uint8* codeSkip = stack.m_code + skipSize; 

	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Evaluate the new context
	THandle< IScriptable > newContextHandle;
	stack.Step( context, &newContextHandle, script::GetRTTIType< THandle< IScriptable > >() );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	// If context is valid, execute in new context
	IScriptable* newContext = newContextHandle.Get();
	if ( newContext )
	{
		// Execute rest of the code in new context
		stack.Step( newContext, result, resultType );
	}
	else
	{
		// Error !
		SCRIPT_RUNTIME_WARN_ONCE( stack, "Accessing invalid handle" );

		// Skip the code
		stack.m_code = codeSkip;

		// Clear the data pointer
		stack.m_lValuePtr = nullptr;
		stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
		stack.m_lValueProperty = nullptr;
#endif

	}
}

void OpStructMember( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Read structure property to access
	const auto* structProp = stack.Read< const rtti::Property* >();
	RED_FATAL_ASSERT( structProp != nullptr, "Expecting structure member property" );

	// Get the data pointer
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	stack.Step( context, nullptr, nullptr );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	if ( stack.m_lValuePtr )
	{
		// Get the data
		void* propData = structProp->GetOffsetPtr( stack.m_lValuePtr );
		stack.m_lValuePtr = propData;
		stack.m_lValueType = structProp->GetType();
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
		// NOTE: We don't want struct property set here because we're only interested in root (class) properties
#endif

		// Copy data
		if ( result )
		{
			RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

			structProp->GetType()->Copy( result, propData );
		}
	}
	else
	{
		// Error !
		SCRIPT_RUNTIME_WARN_ONCE( stack, "Invalid structure member access" );
	}
}

void OpAssign( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Evaluate left side to get the target address
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	stack.Step( context, nullptr, nullptr );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// No place to write the variable to
	const rtti::IType* destType = stack.m_lValueType;

	void* destPtr = stack.m_lValuePtr;
	if ( !destPtr )
	{
		// Emit error
		SCRIPT_RUNTIME_ERROR( stack, "Trying to assign value from invalid context" );

		// Clear buffer - we don't store the type any more so use a large enough buffer here
		// (was to costly + was causing issues with 32/64 bit compatibility)
		const Uint32 typeSize = 512;
		destPtr = RED_ALLOCA( typeSize );
		red::Memset( destPtr, 0, typeSize );
		stack.m_lValueType = nullptr;
		destType = nullptr;
	}

	// Write target to destination
	stack.Step( context, destPtr, destType );

	// Copy result of assignment to 'result' if it is required
	if ( result && destPtr && destType && !stack.m_canElideCopy && Helper::ValidateResultType( resultType, destType ) )
	{
		destType->Copy( result, destPtr );
	}

}

void OpJump( IScriptable*, CScriptStackFrame& stack, void*, const rtti::IType* )
{
	// Jump
	Int16 offset = stack.Read<Int16>();
	stack.m_code += offset; 
}

void OpJumpIfFalse( IScriptable* context, CScriptStackFrame& stack, void*, const rtti::IType* )
{
	// Get the offset to jump
	Int16 offset = stack.Read<Int16>();
	const Uint8* codeSkip = stack.m_code + offset; 

	// Evaluate expression
	Bool condition = false;
	stack.Step( context, &condition, script::GetRTTIType< Bool >() );

	// Execute jump
	if ( !condition )
	{
		stack.m_code = codeSkip;
	}
}

void OpConditional( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the offsets to jump
	const Int16 falseOffset = stack.Read< Int16 >();
	const Uint8* codeFalse = stack.m_code + falseOffset;
	const Int16 endOffset = stack.Read< Int16 >();
	const Uint8* codeEnd = stack.m_code + endOffset; 

	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Evaluate expression
	Bool condition = false;
	stack.Step( context, &condition, script::GetRTTIType< Bool >() );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	if ( condition )
	{
		stack.Step( context, result, resultType );
		stack.m_code = codeEnd;
	}
	else
	{
		stack.m_code = codeFalse;
		stack.Step( context, result, resultType );
	}
}

void OpSwitch( IScriptable* context, CScriptStackFrame& stack, void*, const rtti::IType* )
{
	// Read the type of the switch expression
	const auto* exprType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( exprType != nullptr, "Expecting type reference" );

	// Read the offset to first label
	Int16 skipOffset = stack.Read<Int16>();
	const Uint8* switchLabel = stack.m_code + skipOffset; 
	RED_FATAL_ASSERT( *switchLabel == OP_SwitchLabel || *switchLabel == OP_SwitchDefault, "Code buffer error" );

	// Create expression buffer
	void* propData1 = RED_ALLOCA( exprType->GetSize() );
	red::Memzero( propData1, exprType->GetSize() );
	exprType->Construct( propData1 );

	// Evaluate expression
	stack.Step( context, propData1, exprType );

	// Create comparison buffer 
	void* propData2 = RED_ALLOCA( exprType->GetSize() );
	red::Memzero( propData2, exprType->GetSize() );
	exprType->Construct( propData2 );

	// Process linked list
	Bool handled = false;
	while ( *switchLabel == OP_SwitchLabel )
	{	
		// Skip the opcode
		RED_FATAL_ASSERT( *switchLabel == OP_SwitchLabel , "Code buffer error" );
		stack.m_code = switchLabel + 1;

		// Get the offset to next label
		const Int16 skipOffset = stack.Read<Int16>();
		const Uint8* nextSwitchLabel = stack.m_code + skipOffset; 
		const Int16 targetOffset = stack.Read<Int16>();
		const Uint8* switchTargetCode = stack.m_code + targetOffset; 

		// Load value
		stack.Step( context, propData2, exprType );

		// Compare label value with switch expression
		if ( exprType->Compare( propData1, propData2, 0 ) )
		{
			handled = true;
			stack.m_code = switchTargetCode;
			break;
		}

		// Default case
		if ( *nextSwitchLabel == OP_SwitchDefault )
		{
			stack.m_code = nextSwitchLabel + 1;
			handled = true;
			break;
		}

		// Skip to next
		switchLabel = nextSwitchLabel;
	}

	// Log any non supported case
	if ( !handled )
	{
		// Continue at break label
		stack.m_code = switchLabel;
	}

	// Cleanup
	exprType->Destruct( propData1 );
	exprType->Destruct( propData2 );
}

void OpSwitchCase( IScriptable*, CScriptStackFrame& stack, void*, const rtti::IType* )
{
	// Get the offset to next label
	/*Int16 skipOffset =*/ stack.Read<Int16>();
	Int16 exprSkipOffset = stack.Read<Int16>();

	// Skip the expression
	const Uint8* codeSkip = stack.m_code + exprSkipOffset; 
	stack.m_code = codeSkip;
}

void OpSwitchCaseDefault( IScriptable*, CScriptStackFrame& stack, void*, const rtti::IType* )
{
	// Do nothing
}

void OpTestEqual( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the types
	const auto* type = stack.Read< const rtti::IType* >();
	
	// Allocate buffer for first data
	void* dataA = RED_ALLOCA( type->GetSize() );
	red::Memset( dataA, 0, type->GetSize() );
	type->Construct( dataA );

	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get first data
	stack.Step( context, dataA, type );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	// Allocate buffer for second data
	void* dataB = RED_ALLOCA( type->GetSize() );
	red::Memset( dataB, 0, type->GetSize() );
	type->Construct( dataB );

	// Get first data
	stack.Step( context, dataB, type );

	// Compare data
	Bool isEqual = type->Compare( dataA, dataB, 0 );
	
	// Destroy data
	type->Destruct( dataA );
	type->Destruct( dataB );

	// Return result
	RETURN_BOOL( isEqual );
}

void OpTestNotEqual( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the types
	const auto* type = stack.Read< const rtti::IType* >();

	// Allocate buffer for first data
	void* dataA = RED_ALLOCA( type->GetSize() );
	red::Memset( dataA, 0, type->GetSize() );
	type->Construct( dataA );

	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get first data
	stack.Step( context, dataA, type );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	// Allocate buffer for second data
	void* dataB = RED_ALLOCA( type->GetSize() );
	red::Memset( dataB, 0, type->GetSize() );
	type->Construct( dataB );

	// Get first data
	stack.Step( context, dataB, type );

	// Compare data
	Bool isEqual = type->Compare( dataA, dataB, 0 );

	// Destroy data
	type->Destruct( dataA );
	type->Destruct( dataB );

	// Return result
	RETURN_BOOL( !isEqual );
}

void OpHandleToBool( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	THandle< IScriptable > value;
	stack.Step( context, &value, script::GetRTTIType< THandle< IScriptable > >() );
	RETURN_BOOL( value.Get() == nullptr ? false : true );
}

void OpWeakHandleToBool( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	WeakHandle< IScriptable > value;
	stack.Step( context, &value, script::GetRTTIType< WeakHandle< IScriptable > >() );
	RETURN_BOOL( value.Expired() ? false : true );
}

namespace Helper
{
	static void TypeToString( const rtti::IType* type, const void* data, String& ret )
	{
		if ( type->GetType() == RT_Handle )
		{
			const SerializableHandle& handle = *(const SerializableHandle*) data;
			if ( !handle )
			{
				ret = "NULL";
			}
			else
			{
				ret = String::Printf( "[%hs]", handle.Get()->GetClass()->GetName().AsChar() );
			}
		}
		else
		{
			type->ToString( data, ret );
		}
	}
}

void OpToString( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the types
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( type, "Invalid enum type" );

	// Get the data
	const Uint32 dataSize = type->GetSize();
	if ( dataSize < 4096 )
	{
		// get data
		Uint8 buffer[ 4096 ];
		red::Memzero( buffer, dataSize );
		stack.Step( context, &buffer, type );

		// Convert to string
		String string;
		Helper::TypeToString( type, buffer, string );
		RETURN_STRING( string );
	}
	else
	{
		// allocate dynamic buffer
		DynArray< Uint8 > dynamicBuffer{ red::PoolScript() };
		dynamicBuffer.Resize( dataSize );
		red::Memzero( dynamicBuffer.Data(), dataSize );

		// get data
		stack.Step( context, dynamicBuffer.Data(), resultType );

		// Convert to string
		String string;
		Helper::TypeToString( type, dynamicBuffer.Data(), string );
		RETURN_STRING( string );
	}
}

void OpEnumToInt( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read enum type and int size
	const auto* enumType = stack.Read< const rtti::IType* >();
	const Uint8 intSize = stack.Read< Uint8 >();
	RED_FATAL_ASSERT( enumType->GetSize() <= intSize, "Invalid cast: enum size is bigger than int size." );
	RED_FATAL_ASSERT( result != nullptr, "Invalid result" );

	// Read enum value
	Uint8 buffer[ 8 ] = {};
	stack.Step( context, buffer, enumType );
	Int64 value = -1;

	switch ( enumType->GetSize() )
	{
	case 1: 
		value = *reinterpret_cast< Int8* >( &buffer );
		break;
	case 2:
		value = *reinterpret_cast< Int16* >( &buffer );
		break;
	case 4: 
		value = *reinterpret_cast< Int32* >( &buffer );
		break;
	case 8: 
		value = *reinterpret_cast< Int64* >( &buffer );
		break;
	}

	switch ( intSize )
	{
	case 1:
		*reinterpret_cast< Int8* >( result ) = static_cast< Int8 >( value );
		break;
	case 2:
		*reinterpret_cast< Int16* >( result ) = static_cast< Int16 >( value );
		break;
	case 4:
		*reinterpret_cast< Int32* >( result ) = static_cast< Int32 >( value );
		break;
	case 8: 
		*reinterpret_cast< Int64* >( result ) = static_cast< Int64 >( value );
		break;
	}	
}

void OpIntToEnum( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read enum type and int size
	const auto* enumType = stack.Read< const rtti::IType* >();
	const Uint8 intSize = stack.Read< Uint8 >();
	RED_FATAL_ASSERT( enumType->GetSize() >= intSize, "Invalid cast: int size is bigger than enum size." );
	RED_FATAL_ASSERT( result != nullptr, "Invalid result" );

	// Read int
	Uint8 buffer[ 8 ] = {};
	stack.Step( context, buffer, nullptr );
	Int64 value = -1;

	switch ( intSize )
	{
	case 1: 
		value = *reinterpret_cast< Int8* >( &buffer );
		break;
	case 2:
		value = *reinterpret_cast< Int16* >( &buffer );
		break;
	case 4: 
		value = *reinterpret_cast< Int32* >( &buffer );
		break;
	case 8: 
		value = *reinterpret_cast< Int64* >( &buffer );
		break;
	}

	switch ( enumType->GetSize() )
	{
	case 1:
		*reinterpret_cast< Int8* >( result ) = static_cast< Int8 >( value );
		break;
	case 2:
		*reinterpret_cast< Int16* >( result ) = static_cast< Int16 >( value );
		break;
	case 4:
		*reinterpret_cast< Int32* >( result ) = static_cast< Int32 >( value );
		break;
	case 8: 
		*reinterpret_cast< Int64* >( result ) = static_cast< Int64 >( value );
		break;
	}
}

void OpDynamicCast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read the cast type
	const auto* castToType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( castToType && castToType->GetType() == RT_Class, "Expecting class type" );

	const Uint8 isWeakHandle = stack.Read< Uint8 >();

	// Get class we want to cast to
	const rtti::ClassType* castToClass = static_cast< const rtti::ClassType* >( castToType );

	// Read handle
	if ( isWeakHandle )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, script::GetRTTIType< WeakHandle< IScriptable > >() ), "Invalid result type" );

		WeakHandle< IScriptable > weakHandle;
		stack.Step( context, &weakHandle, script::GetRTTIType< WeakHandle< IScriptable > >() );

		// Return casted handle
		if ( result )
		{
			// Get object
			if ( THandle< IScriptable > handle = weakHandle.ToHandle() )
			{
				if ( handle && !handle->IsA( castToClass ) )
				{
					weakHandle = nullptr;
				}

				// Copy to result
				(*( WeakHandle< IScriptable >* ) result) = weakHandle;
			}
			else
			{
				// Copy to result
				(*( WeakHandle< IScriptable >* ) result) = nullptr;
			}
		}
	}
	else
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, script::GetRTTIType< THandle< IScriptable > >() ), "Invalid result type" );

		THandle< IScriptable > handle;
		stack.Step( context, &handle, script::GetRTTIType< THandle< IScriptable > >() );

		// Return casted handle
		if ( result )
		{
			// Get object
			if ( handle && !handle->IsA( castToClass ) )
			{
				handle = nullptr;
			}

			// Copy to result
			(*( THandle< IScriptable >* ) result) = handle;
		}
	}
}

void OpConstructor( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get number of parameters
	const Uint8 numParameters = stack.Read<Uint8>();

	// Get the type being constructed
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( type && type->GetType() == RT_Class, "Expecting class type" );

	// No result pointer given, skip parameters
	if ( !result )
	{
		for ( Uint32 i=0; i<numParameters; i++ )
			stack.Step( context, nullptr, nullptr );
	}
	else
	{
		// gtatoulis 2021-01-12. This is quick fix for memory leaking when OpConstructor is called as part of OpAssign 
		// myVar = Struct();
		// on a variable that was already in use with non-POD members. This will overwrite the memory leading to a leak.
		// I've checked the various uses of OpConstructer (on Params, variables, references) I could find and everything looks constructed before 
		// this call is used so this looks ok but I can't guarantee this 100% safe but we have a known leak it is causing so I'm adding it to fix that
		// Ideally this would be better with OpMove/OpDestruct (that don't exist yet) built into the language parsing during the assignment. 
		if( resultType && resultType->NeedsCleaning() )
		{
			resultType->Destruct( result );
		}

		// Construct type
		type->Construct( result );

		// Get properties to initialize
		const rtti::ClassType* classType = static_cast< const rtti::ClassType* >( type );
		const auto& props = classType->GetCachedProperties();

		// Initialize structure properties
		RED_FATAL_ASSERT( numParameters <= props.Size(), "Constructor trying to write more params than allowed" );
		for ( int i = 0; i < numParameters; ++i )
		{
			const auto& prop = props[i];
			void* targetData = prop->GetOffsetPtr( result );
			stack.Step( context, targetData, prop->GetType() );
		}
	}
}

// helper class that filters which scriptable classes can have memory managment enabled and which not (tempshit)
class CScriptMemoryManagmentFilter
{
public:
	HashMap< const rtti::ClassType*, Bool >		m_allowed{ red::PoolScript() };
	HashMap< const rtti::ClassType*, Bool >		m_visited{ red::PoolScript() };

	CScriptMemoryManagmentFilter()
	{
		//m_allowed.Set( ClassID< IScriptable >(), false );		
	}

	void AddClass( const Char* className )
	{
		const rtti::ClassType* objectClass = GetRttiSystem().FindClass( RED_NAME_NOREG( UNICODE_TO_ANSI( className ) ) );
		if ( nullptr != objectClass )
		{
			m_allowed.Set( objectClass, true );
		}
	}

	const Bool CanMemoryManager( const rtti::ClassType* objectClass )
	{
		// get from cache
		Bool reportFlag = true;
		if ( m_visited.Find( objectClass, reportFlag ) )
		{
			return reportFlag;
		}

		// return true if given class is allowed to be memory managed by THandle system
		const rtti::ClassType* curClass = objectClass;
		while ( nullptr != curClass )
		{
			Bool canManage = false;
			if ( m_allowed.Find( curClass, canManage ) )
			{
				reportFlag = canManage;
				break;
			}

			curClass = curClass->GetBaseClass();
		}

		// stats
		RED_LOG( "Core: Class '%hs' memory managment is '%hs'", objectClass->GetName().AsChar(), reportFlag ? "ENABLED" : "DISABLED" );
		m_visited.Set( objectClass, reportFlag );
		return reportFlag;
	}

	static CScriptMemoryManagmentFilter& GetInstance()
	{
		static CScriptMemoryManagmentFilter theInstance;
		return theInstance;
	}
};

void OpNew( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Read class
	const auto* objectClass = stack.Read< const rtti::ClassType* >();

	// Create object
	THandle< IScriptable > newObject;
	newObject = objectClass->CreateHandle< IScriptable >();

	// Warn
	SCRIPT_RUNTIME_ERROR_CONDITION( !newObject, stack, "Unable to create object '%s'", objectClass->GetName().AsChar() );

	// Write to result
	RETURN_OBJECT( newObject );
}

void OpThis( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RETURN_OBJECT( HandleFromPtr( context ) );
}

void OpArraySize( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArraySize( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayPushBack( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayPushBack( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayPopBack( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayPopBack( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayInsert( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayInsert( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayRemove( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayRemove( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayRemoveFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayRemoveFast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayErase( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayErase( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayEraseFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayEraseFast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayClear( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayClear( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayResize( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayResize( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayGrow( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayGrow( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayContains( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayContains( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayContainsFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayContainsFast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayCount( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayCount( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayCountFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayCountFast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayFindFirst( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayFindFirst( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayFindFirstFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected array type" );
	script::ArrayFindFirstFast( static_cast< const rtti::IBaseArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayFindLast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayFindLast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayFindLastFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayFindLastFast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayLast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayLast( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpArrayElement( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_Array, "Expected dynamic array type" );
	script::ArrayElement( static_cast< const rtti::ArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArraySize( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArraySize( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayContains( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayContains( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayContainsFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayContainsFast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayCount( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayCount( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayCountFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayCountFast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayFindFirst( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayFindFirst( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayFindFirstFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayFindFirstFast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayFindLast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayFindLast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayFindLastFast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayFindLastFast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayLast( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayLast( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpStaticArrayElement( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const auto* arrayType = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( arrayType && arrayType->GetType() == RT_NativeArray, "Expected static array type" );
	script::ArrayElement( static_cast< const rtti::NativeArrayType* >( arrayType ), context, stack, result, resultType );
}

void OpNeverCall( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	RED_FATAL( "This opcode should never be called directly" );
}

namespace helper
{
	class BufferForTypeData : public red::NonCopyable
	{
	public:
		RED_FORCE_INLINE BufferForTypeData(const rtti::IType* type)
			: m_type(nullptr)
		{
			m_buffer = m_localData;

			if (type->GetSize() > sizeof(m_localData))
				m_buffer = RED_ALLOCATE( red::PoolEngine, type->GetSize() );

			red::Memzero(m_buffer, type->GetSize());

			if (type->NeedsCleaning())
			{
				type->Construct(m_buffer);
				m_type = type;
			}
		}

		RED_FORCE_INLINE ~BufferForTypeData()
		{
			if (m_type)
				m_type->Destruct(m_buffer);

			if (m_buffer != m_localData)
				RED_FREE( red::PoolEngine, m_buffer );
		}

		RED_FORCE_INLINE void* GetData()
		{
			return m_buffer;
		}

	private:
		void*		m_buffer;
		Uint8		m_localData[256];

		const rtti::IType*	m_type;
	};
} // helper

void OpCastToVariant( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the type being casted from
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT(type != nullptr, "Expecting type reference");

	// direct write
	if (result != nullptr)
	{
		// create a variant of given type
		auto* variant = (rtti::Variant*)result;
		variant->Set(type, nullptr);

		// Get the data
		stack.m_lValuePtr = nullptr;
		stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
		stack.m_lValueProperty = nullptr;
#endif
		stack.Step( context, const_cast< void* >( variant->GetData() ), variant->GetRTTIType() );
	}
	else
	{
		// dummy step
		stack.m_lValuePtr = nullptr;
		stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
		stack.m_lValueProperty = nullptr;
#endif
		stack.Step( context, nullptr, nullptr );
	}
}

void OpCastFromVariant( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the type being casted from
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT(type != nullptr, "Expecting type reference");

	// evaluate the variant
	// TODO: we need to do a version of the OpCastFromVariant that works directly from the memory
	rtti::Variant variant;
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, &variant, script::GetRTTIType< rtti::Variant >() );

	// check if type matches
	if ( type != variant.GetRTTIType() )
	{
		SCRIPT_RUNTIME_WARN_ONCE( stack, "Casting from variant of type '%hs' to '%hs'", variant.GetTypeName().AsChar(), type->GetName().AsChar() );
		return;
	}

	// copy
	if ( result )
	{
		type->Copy(result, variant.GetData());
	}
}

void OpCastToRef( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the type being casted from
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( type != nullptr, "Expecting type reference" );

	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	if ( result )
	{
		auto* reference = static_cast< rtti::ScriptedReferenceType* >( result );
		void* referencedObj = reference->GetReferencedObject();
		if ( referencedObj )
		{
			RED_FATAL_ASSERT( Helper::ValidateResultType( reference->GetPointedType(), type ), "Invalid result type" );

			stack.Step( context, referencedObj, type );
		}
		else
		{
			stack.Step( context, nullptr, nullptr );

			if ( result != nullptr )
			{
				if ( stack.m_lValuePtr && stack.m_lValueType )
				{
					RED_FATAL_ASSERT( Helper::ValidateResultType( stack.m_lValueType, type ), "Invalid result type" );
					reference->SetReference( type, stack.m_lValuePtr );
				}
			}
		}
	}
	else
	{
		// this should never happen?
		stack.Step( context, nullptr, nullptr );
	}
}

void OpCastFromRef( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	// Get the type being casted from
	const auto* type = stack.Read< const rtti::IType* >();
	RED_FATAL_ASSERT( type != nullptr, "Expecting type reference" );

	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif

	rtti::ScriptedReferenceType reference;
	stack.Step( context, &reference, script::GetRTTIType< rtti::ScriptedReferenceType >() );

	// in case of simple dereference store referenced object in m_lValuePtr
	stack.m_lValuePtr = reference.GetReferencedObject();
	stack.m_lValueType = reference.GetPointedType();

	if ( result != nullptr )
	{
		// make a deep copy when we need to store dereferenced object
		if ( stack.m_lValuePtr && stack.m_lValueType )
		{
			RED_FATAL_ASSERT( Helper::ValidateResultType( type, resultType ), "Invalid result type" );
			type->Copy( result, stack.m_lValuePtr );
		}
	}
}

void OpVariantIsValid(IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, nullptr, nullptr );

	auto* variant = (rtti::Variant*)stack.m_lValuePtr;
	RETURN_BOOL(variant && variant->IsValid());
}

void OpVariantIsHandle(IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, nullptr, nullptr );

	auto* variant = (rtti::Variant*)stack.m_lValuePtr;
	RETURN_BOOL(variant && variant->GetRTTIType() && variant->GetRTTIType()->GetType() == RT_Handle);
}

void OpVariantIsArray(IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, nullptr, nullptr );

	auto* variant = (rtti::Variant*)stack.m_lValuePtr;
	RETURN_BOOL(variant && variant->IsArray());
}

void OpVariantGetTypeName(IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, nullptr, nullptr );

	auto* variant = (rtti::Variant*)stack.m_lValuePtr;
	RETURN_NAME(variant ? variant->GetTypeName() : CName::NONE());
}

void OpVariantToString(IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = nullptr;
#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY
	stack.m_lValueProperty = nullptr;
#endif
	stack.Step( context, nullptr, nullptr );

	auto* variant = (rtti::Variant*)stack.m_lValuePtr;

	if ( result != nullptr )
	{
		RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, script::GetRTTIType< String >() ), "Invalid result type" );
		variant->ValueToString(*(String*)result);
	}
}

void OpWeakToStrongHandle( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType)
{
	const rtti::IType* expectedResultType = script::GetRTTIType< THandle< IScriptable > >();
	RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, expectedResultType ), "Invalid result type" );
	RED_FATAL_ASSERT( result != nullptr, "Invalid result" );

	WeakHandle< IScriptable > weakHandle;
	stack.Step( context, &weakHandle, script::GetRTTIType< WeakHandle< IScriptable > >() );
	*reinterpret_cast< THandle< IScriptable >* >( result ) = weakHandle.ToHandle();
}

void OpStrongToWeakHandle( IScriptable* context, CScriptStackFrame& stack, void* result, rtti::IType* resultType )
{
	const rtti::IType* expectedResultType = script::GetRTTIType< WeakHandle< IScriptable > >();
	RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, expectedResultType ), "Invalid result type" );
	RED_FATAL_ASSERT( result != nullptr, "Invalid result" );

	THandle< IScriptable > strongHandle;
	stack.Step( context, &strongHandle, script::GetRTTIType< THandle< IScriptable > >() );
	*reinterpret_cast< WeakHandle< IScriptable >* >( result ) = strongHandle;
}

#define OpTarget OpNeverCall
#define OpSwitchLabel OpNeverCall
#define OpSwitchDefault OpNeverCall
#define OpSkip OpNeverCall
#define OpParamEnd OpNeverCall
#define OpMax OpNeverCall
#define OpDelete OpNeverCall

void ExportCoreOpcodes()
{
#define OPCODE(x) CScriptNativeFunctionMap::SetOpcode( OP_##x, (TNativeGlobalFunc) &Op##x );
#include "scriptOpcodesList.h"
#undef OPCODE
}
