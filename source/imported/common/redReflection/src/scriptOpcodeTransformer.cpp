/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptOpcodes.h"
#include "scriptOpcodeTransformer.h"

CScriptOpcodeTransformer::CScriptOpcodeTransformer()
{
}

CScriptOpcodeTransformer::~CScriptOpcodeTransformer()
{
}

void CScriptOpcodeTransformer::PerformTransform()
{
	while ( !EndOfStream() )
	{
		// Rewrite opcode to stream
		const Uint8 opcode = ProcessOpcode();
		//fprintf( stderr, "OpCode: %d (%hs)\n", opcode, GetOpcodeName((EScriptOpcode)opcode) );
		//RED_FATAL_ASSERT( 0 != strcmp( GetOpcodeName((EScriptOpcode)opcode), "Unknown"), "Invalid opcode" );

		// Write opcode data 
		switch ( opcode )
		{
			// Breakpoint
			case OP_Breakpoint:
			{
				ProcessBreakpoint();
				break;
			}

			case OP_StartProfile:
			{
				ProcessStartProfile();
				break;
			}

			// Constant
			case OP_IntConst1:
			{
				ProcessInt8();
				break;
			}   

			case OP_IntConst2:
			{
				ProcessInt16();
				break;
			}   

			case OP_IntConst4:
			{
				ProcessInt32();
				break;
			}   

			case OP_IntConst8:
			{
				ProcessInt64();
				break;
			}

			case OP_UintConst1:
			{
				ProcessUint8();
				break;
			}

			case OP_UintConst2:
			{
				ProcessUint16();
				break;
			}

			case OP_UintConst4:
			{
				ProcessUint32();
				break;
			}

			case OP_UintConst8:
			{
				ProcessUint64();
				break;
			}

			// Float constant
			case OP_FloatConst:
			{
				ProcessFloat();
				break;
			}

			// Double constant
			case OP_DoubleConst:
			{
				ProcessDouble();
				break;
			}

			// string constant
			case OP_StringConst:
			{
				ProcessString();
				break;
			}

			// Name constant
			case OP_NameConst:
			{
				ProcessName();
				break;
			}

			// Named value constant
			case OP_EnumConst:
			{
				ProcessEnum();
				ProcessNamedValueRef();
				break;
			}

			// TweakDBID constant
			case OP_TweakDBIDConst:
			{
				ProcessTweakDBID();
				break;
			}

			// ResRef constant
			case OP_ResRefConst:
			{
				ProcessResRef();
				break;
			}

			// Switch
			case OP_Switch:
			{
				ProcessTypeRef();
				ProcessLabel();
				break;
			}

			// Switch label
			case OP_SwitchLabel:
			{
				ProcessLabel();
				ProcessLabel();
				break;
			}     

			// Default switch
			case OP_SwitchDefault:
			{
				break;
			}

			// Jumps
			case OP_Jump:
			case OP_Skip:   
			case OP_JumpIfFalse:
			{
				ProcessLabel();
				break;   
			};   

			// Conditional expression
			case OP_Conditional:
			{
				ProcessLabel();
				ProcessLabel();
				break;
			}

			// Construct struct
			case OP_Constructor:
			{
				ProcessUint8();
				ProcessPointer();
				break;
			};   

			// Function call
			case OP_FinalFunc:
			{
				// instead of label
				ProcessUint16();
				ProcessUint16();
				ProcessFunctionRef();
				break;
			}

			// Virtual function
			case OP_VirtualFunc:
			{
				ProcessLabel();
				ProcessUint16();
				ProcessName();
				break;
			}

			// Context
			case OP_Context:
			{
				ProcessLabel();
				break;
			}

			// Properties
			case OP_ObjectVar:
			case OP_StructMember:
			{
				ProcessPropertyRef();
				break;
			}
			
			case OP_ParamVar:
			{
				ProcessParamPropertyRef();
				break;
			}

			case OP_LocalVar:
			{
				ProcessLocalPropertyRef();
				break;
			}

			// New
			case OP_New:
			{
				ProcessPointer();
				break;
			}

			// Dynamic cast
			case OP_DynamicCast:
			{
				ProcessPointer();
				ProcessUint8();
				break;
			}

			// Comparisons
			case OP_TestEqual:
			case OP_TestNotEqual:

			// Dynamic casting
			case OP_ToString:
			case OP_CastToVariant:
			case OP_CastFromVariant:
			case OP_CastToRef:
			case OP_CastFromRef:
			{
				ProcessTypeRef();
				break;
			}

			case OP_EnumToInt:
			case OP_IntToEnum:
			{
				ProcessTypeRef();
				ProcessUint8();
				break;
			}

			// Array opcodes
			case OP_ArrayClear:
			case OP_ArraySize:
			case OP_ArrayResize:
			case OP_ArrayFindFirst:
			case OP_ArrayFindFirstFast:
			case OP_ArrayFindLast:
			case OP_ArrayFindLastFast:
			case OP_ArrayContains:
			case OP_ArrayContainsFast:
			case OP_ArrayCount:
			case OP_ArrayCountFast:
			case OP_ArrayPushBack:
			case OP_ArrayPopBack:
			case OP_ArrayInsert:
			case OP_ArrayRemove:
			case OP_ArrayRemoveFast:
			case OP_ArrayGrow:
			case OP_ArrayErase:
			case OP_ArrayEraseFast:
			case OP_ArrayLast:
			case OP_ArrayElement:
			{
				ProcessTypeRef();
				break;
			}

			// Static array opcodes
			case OP_StaticArraySize:
			case OP_StaticArrayFindFirst:
			case OP_StaticArrayFindFirstFast:
			case OP_StaticArrayFindLast:
			case OP_StaticArrayFindLastFast:
			case OP_StaticArrayContains:
			case OP_StaticArrayContainsFast:
			case OP_StaticArrayCount:
			case OP_StaticArrayCountFast:
			case OP_StaticArrayLast:
			case OP_StaticArrayElement:
			{
				ProcessTypeRef();
				break;
			}
		}; 
	}
}