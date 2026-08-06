/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#ifndef OPCODE
#define OPCODE(x)  OP_##x,
#define OPCODE_ENUM
enum EScriptOpcode {
#endif

	OPCODE(Nop)							//!< No operation
	OPCODE(Null)						//!< ISerializable* NULL
	OPCODE(IntOne)						//!< Integer '1'
	OPCODE(IntZero)						//!< Integer '0'
	OPCODE(IntConst1)					//!< Numerical constant value 1 byte (signed)
	OPCODE(IntConst2)					//!< Numerical constant value 2 bytes (signed)
	OPCODE(IntConst4)					//!< Numerical constant value 4 bytes (signed)
	OPCODE(IntConst8)					//!< Numerical constant value 8 bytes (signed)
	OPCODE(UintConst1)					//!< Numerical constant value 1 byte (unsigned)
	OPCODE(UintConst2)					//!< Numerical constant value 2 bytes (unsigned)
	OPCODE(UintConst4)					//!< Numerical constant value 4 bytes (unsigned)
	OPCODE(UintConst8)					//!< Numerical constant value 8 bytes (unsigned)
	OPCODE(FloatConst)					//!< Float constant
	OPCODE(DoubleConst)					//!< Double constant
	OPCODE(NameConst)					//!< Name constant
	OPCODE(EnumConst)					//!< Enum constant (via NamedValue ref)
	OPCODE(StringConst)					//!< String constant
	OPCODE(TweakDBIDConst)				//!< TweakDBID constant
	OPCODE(ResRefConst)					//!< ResRef constant
	OPCODE(BoolTrue)					//!< True
	OPCODE(BoolFalse)					//!< False
	OPCODE(Breakpoint)					//!< Breakpoint wrapper) generated only in debug code
	OPCODE(Assign)						//!< Assign value
	OPCODE(Target)						//!< Target of a label
	OPCODE(LocalVar)					//!< Access to local variable
	OPCODE(ParamVar)					//!< Access to function parameter variable
	OPCODE(ObjectVar)					//!< Access to object variable
	OPCODE(ExternalVar)					//!< Access to data stored in some external memory - given explicitly as void* , NOTE: this is NEVER SAVED in opcode stream and is only used for the call translation
	OPCODE(Switch)						//!< Switch statement
	OPCODE(SwitchLabel)					//!< Label in switch statement
	OPCODE(SwitchDefault)				//!< Default switch statement
	OPCODE(Jump)						//!< Jump to target
	OPCODE(JumpIfFalse)					//!< Jump if condition is false
	OPCODE(Skip)						//!< Skippable block
	OPCODE(Conditional)					//!< Conditional expression
	OPCODE(Constructor)					//!< Constructor
	OPCODE(FinalFunc)					//!< Call to final function ( static function binding )
	OPCODE(VirtualFunc)					//!< Call to virtual function
	OPCODE(ParamEnd)					//!< End of parameters
	OPCODE(Return)						//!< Return from function
	OPCODE(StructMember)				//!< Access to structure member ( slow )
	OPCODE(Context)						//!< Evaluation context change
	OPCODE(TestEqual)					//!< Test if two given shit is default
	OPCODE(TestNotEqual)				//!< Test if two given shit is default
	OPCODE(New)							//!< Create object
	OPCODE(Delete)						//!< Delete object
	OPCODE(This)						//!< Reference to self
	OPCODE(StartProfile)				//!< Profiler entry function 

	// Array access opcodes
	OPCODE(ArrayClear)					//!< Clear the array
	OPCODE(ArraySize)					//!< Get the size of the array
	OPCODE(ArrayResize)					//!< Resize array
	OPCODE(ArrayFindFirst)				//!< Find index of first matching element from the array
	OPCODE(ArrayFindFirstFast)			//!< Find index of first matching element from the array ( faster version )
	OPCODE(ArrayFindLast)				//!< Find index of last matching element from the array
	OPCODE(ArrayFindLastFast)			//!< Find index of last matching element from the array ( faster version )
	OPCODE(ArrayContains)				//!< Check if array contains a given item
	OPCODE(ArrayContainsFast)			//!< Check if array contains a given item ( faster version )
	OPCODE(ArrayCount)					//!< Count matching elements
	OPCODE(ArrayCountFast)				//!< Count matching elements ( faster version )
	OPCODE(ArrayPushBack)				//!< Add element to array
	OPCODE(ArrayPopBack)				//!< Remove last element from array
	OPCODE(ArrayInsert)					//!< Insert element to array
	OPCODE(ArrayRemove)					//!< Remove element from array
	OPCODE(ArrayRemoveFast)				//!< Remove element from array ( faster version, it doesn't need to create/destroy evaluated argument )
	OPCODE(ArrayGrow)					//!< Add space to array
	OPCODE(ArrayErase)					//!< Erase place in array
	OPCODE(ArrayEraseFast)				//!< Fast erase without preserving order of elements
	OPCODE(ArrayLast)					//!< Get the last element from array
	OPCODE(ArrayElement)				//!< Access to array element

	// Static array access opcodes
	OPCODE(StaticArraySize)				//!< Get the size of the static array
	OPCODE(StaticArrayFindFirst)		//!< Find index of first matching element from the static array
	OPCODE(StaticArrayFindFirstFast)	//!< Find index of first matching element from the static array ( faster version )
	OPCODE(StaticArrayFindLast)			//!< Find index of last matching element from the static array
	OPCODE(StaticArrayFindLastFast)		//!< Find index of last matching element from the static array ( faster version )
	OPCODE(StaticArrayContains)			//!< Check if static array contains a given item
	OPCODE(StaticArrayContainsFast)		//!< Check if static array contains a given item ( faster version )
	OPCODE(StaticArrayCount)			//!< Count matching elements
	OPCODE(StaticArrayCountFast)		//!< Count matching elements ( faster version )
	OPCODE(StaticArrayLast)				//!< Get the last element from static array
	OPCODE(StaticArrayElement)			//!< Access to array static element

	// Hardcoded casts
	OPCODE(HandleToBool)				//!< Checks if the input strong handle is not NULL and returns true, otherwise it returns false
	OPCODE(WeakHandleToBool)			//!< Checks if the input weak handle is not NULL and returns true, otherwise it returns false
	OPCODE(EnumToInt)					//!< Understands the enum type and is able to convert the enum value to numerical value
	OPCODE(IntToEnum)					//!< Understands the enum type and is able to convert numerical value to enum
	OPCODE(DynamicCast)					//!< Dynamic downcast from a class to derived class (wrapped IsA)
	OPCODE(ToString)					//!< General ToString cast for ANY type

	// Variant
	OPCODE(CastToVariant)				//!< Converts any value to a variant (type required)
	OPCODE(CastFromVariant)				//!< Extract any value from a variant (type required)
	OPCODE(VariantIsValid)				//!< Check if variant value is valid
	OPCODE(VariantIsHandle)				//!< Check if variant value is a handle
	OPCODE(VariantIsArray)				//!< Check if variant value is an array
	OPCODE(VariantGetTypeName)			//!< Get type name of the variant
	OPCODE(VariantToString)				//!< Convert variant value to string (debug)

	OPCODE(WeakToStrongHandle)			//!< Convert weak handle to strong handle
	OPCODE(StrongToWeakHandle)			//!< Convert strong handle to weak handle

	OPCODE(WeakNull)					//!< WeakHandle<ISerializable>* NULL

	// reference
	OPCODE(CastToRef)					//!< Converts any value to a reference (type required)
	OPCODE(CastFromRef)					//!< Extract any value from a reference (type required)

	OPCODE(Max)

#ifdef OPCODE_ENUM
};
#undef OPCODE_ENUM
#undef OPCODE
#endif