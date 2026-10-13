/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptArrayFunctions.h"

#include "scriptStackFrame.h"
#include "scriptingSystem.h"
#include "scriptOpcodesUtils.h"
#include "scriptLog.h"
#include "../../redContainers/include/dynArrayAccessor.h"
#include "rttiArrayTypes.h"
#include "rttiArrayTypesImpl.h"
#include "rttiFunction.h"
#include "scriptableThreadSafetyMonitor.h"

namespace
{
	RED_INLINE void ResetValue( const rtti::IType* type, void* result )
	{
		if ( result )
		{
			// first destroy current type
			type->Destruct( result );

			// reset result to default value
			type->Construct( result );
		}
	}
}

namespace script
{

void ArraySize( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Get array size
	if ( stack.m_lValuePtr )
	{
		Uint32 size = arrayType->ArrayGetArraySize( stack.m_lValuePtr );
		RETURN_INT( size );
	}
	else
	{
		RETURN_INT( 0 );
	}
}

void ArrayPushBack( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// We have an array, allocate element
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate element
		Uint32 index = arrayType->AddArrayElement( arrayData, 1, red::PoolScript() );
		void* elementData = arrayType->GetArrayElement( arrayData, index );

		// Set element content
		stack.Step( context, elementData, arrayType->GetInnerType() );
	}
	else
	{
		// Just evaluate the parameter
		stack.Step( context, nullptr, nullptr );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayPopBack( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	const rtti::IType* innerType = arrayType->ArrayGetInnerType();

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );
	void* arrayData = stack.m_lValuePtr;

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Reset context
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = innerType;

	// Get the index of element to erase
	if ( arrayData )
	{
		// Check that range is valid
		const Int32 arraySize = arrayType->GetArraySize( arrayData );
		if ( arraySize < 1 )
		{
			// Info
			SCRIPT_RUNTIME_ERROR( stack, "Cannot pop element from empty array." );
			SCRIPT_DUMP_STACK_TO_LOG( stack );

			// Clear the result
			ResetValue( innerType, result );
		}
		else
		{
			// Write to result
			void* elementData = arrayType->GetArrayElement( arrayData, arraySize - 1 );
			stack.m_lValuePtr = elementData;

			// Write result
			if ( result )
			{
				RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, innerType ), "Invalid result type" );
				innerType->Copy( result, elementData );
			}

			arrayType->DeleteArrayElement( arrayData, arraySize - 1 );
		}
	}
	else
	{
		// Clear the result
		ResetValue( innerType, result );
	}
}

void ArrayInsert( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );
	void* arrayData = stack.m_lValuePtr;

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Get index
	Int32 index = 0;
	stack.Step( context, &index, script::GetRTTIType< Int32 >() );

	// Reset context
	stack.m_lValuePtr = nullptr;

	if ( arrayData )
	{
		// Check that range is valid
		const Int32 arraySize = arrayType->GetArraySize( arrayData );
		if ( index < 0 || index > arraySize )
		{
			// Info
			SCRIPT_RUNTIME_WARN_ONCE( stack, "Index %i is out of array bounds. Size = %i", index, arraySize );

			// Just evaluate the parameter
			stack.Step( context, nullptr, nullptr );
		}
		else
		{
			// Allocate element
			arrayType->InsertArrayElementAt( arrayData, index );
			void* elementData = arrayType->GetArrayElement( arrayData, index );

			// Set element content
			stack.Step( context, elementData, arrayType->GetInnerType() );
		}
	}
	else
	{
		// Just evaluate the parameter
		stack.Step( context, nullptr, nullptr );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayRemove( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate space for element
		const rtti::IType* innerType = arrayType->GetInnerType();
		void* elementData = RED_ALLOCA( innerType->GetSize() );
		innerType->Construct( elementData );

		// Read element
		stack.Step( context, elementData, arrayType->GetInnerType() );

		// Find the element in the array
		Int32 elementIndex = -1;
		const Uint32 arraySize = arrayType->GetArraySize( arrayData );
		for ( Uint32 i = 0; i < arraySize; i++ )
		{
			const void* arrayElementData = arrayType->GetArrayElement( arrayData, i );
			if ( innerType->Compare( arrayElementData, elementData, 0 ) )
			{
				elementIndex = i;
				break;				
			}
		}

		// Cleanup temporary
		innerType->Destruct( elementData );

		// Remove element
		if ( elementIndex != -1 )
		{
			arrayType->DeleteArrayElement( arrayData, elementIndex );
		}

		// Return true if element was removed
		RETURN_BOOL( elementIndex != -1 );
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_BOOL( false );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayRemoveFast( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get the element to remove
		stack.m_lValuePtr = nullptr;
		stack.Step( context, nullptr, nullptr );
		void* elementData = stack.m_lValuePtr;
		if ( elementData )
		{
			// Find the element in the array
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			Int32 elementIndex = -1;
			const Uint32 arraySize = arrayType->GetArraySize( arrayData );
			for ( Uint32 i=0; i < arraySize; i++ )
			{
				const void* arrayElementData = arrayType->GetArrayElement( arrayData, i );
				if ( innerType->Compare( arrayElementData, elementData, 0 ) )
				{
					elementIndex = i;
					break;				
				}
			}

			// Remove element
			if ( elementIndex != -1 )
			{
				arrayType->DeleteArrayElement( arrayData, elementIndex );
			}

			// Return true if element was removed
			RETURN_BOOL( elementIndex != -1 );
		}
		else
		{
			// Not removed
			RETURN_BOOL( false );
		}
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_BOOL( false );
	}
}

void ArrayErase( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Get the index of element to erase
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get index
		Int32 index = 0;
		stack.Step( context, &index, script::GetRTTIType< Int32 >() );

		// Erase if the index is valid
		const Int32 arraySize = arrayType->GetArraySize( arrayData );
		if ( index >= 0 && index < arraySize )
		{
			// Delete element
			arrayType->DeleteArrayElement( arrayData, index );
			RETURN_BOOL( true );
		}
		else
		{
			SCRIPT_RUNTIME_ERROR( stack, "Cannot delete item in array with index %i (array only has %i elements!)", index, arraySize );
			RETURN_BOOL( false );
		}
	}
	else
	{
		stack.Step( context, nullptr, nullptr );
		RETURN_BOOL( false );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

// faster version, replaces removed element with the last one, thus reorders the array
void ArrayEraseFast( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Get the index of element to erase
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get index
		Int32 index = 0;
		stack.Step( context, &index, script::GetRTTIType< Int32 >() );

		if( arrayType->DeleteArrayElementFast( arrayData, index ) )
		{
			RETURN_BOOL( true );
		}
		else
		{
			const Int32 arraySize = arrayType->GetArraySize( arrayData );
			SCRIPT_RUNTIME_ERROR( stack, "Cannot delete item in array with index %i (array only has %i elements!)", index, arraySize );
			RETURN_BOOL( false );
		}
	}
	else
	{
		stack.Step( context, nullptr, nullptr );
		RETURN_BOOL( false );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayClear( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Clean the array
	void* arrayData = stack.m_lValuePtr;
	ResetValue( arrayType, arrayData );
}

void ArrayResize( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );
	void* arrayData = stack.m_lValuePtr;

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Get new array size
	Int32 arraySize = 0;
	stack.Step( context, &arraySize, script::GetRTTIType< Int32 >() );

	// Array to small
	if ( arraySize < 0 )
	{
		SCRIPT_RUNTIME_ERROR( stack, "Negative array size used in Resize: %i", arraySize );
		arraySize = 0;
	}

	// Resize array
	if ( arrayData )
	{
		Int32 currentSize = arrayType->GetArraySize( arrayData );
		if ( arraySize > currentSize )
		{
			const Int32 numItemsToAdd = arraySize - currentSize;
			arrayType->AddArrayElement( arrayData, numItemsToAdd, red::PoolScript() );
		}
		else if ( arraySize < currentSize )
		{
			// Cleanup elements
			const Uint32 numItemsToRemove = currentSize - arraySize;
			const Uint32 firstElementToRemove = arraySize;
			for ( Uint32 i = 0; i < numItemsToRemove; i++ )
			{
				void* itemData = arrayType->GetArrayElement( arrayData, firstElementToRemove + i );
				arrayType->GetInnerType()->Destruct( itemData );
			}

			// Resize array
			red::DynArrayAccessor& ar = red::DynArrayAccessor::GetRef( arrayData );
			ar.ResizeExact( arraySize, arrayType->GetInnerType()->GetSize(), arrayType->GetInnerType()->GetAlignment() );
		}		
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayGrow( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY();

	// Get the index of element to erase
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get size to alloc
		Int32 growSize = 0;
		stack.Step( context, &growSize, script::GetRTTIType< Int32 >() );

		// Add elements
		if ( growSize > 0 )
		{
			Int32 firstIndex = arrayType->AddArrayElement( arrayData, growSize, red::PoolScript() );
			RETURN_INT( firstIndex );
		}
		else
		{
			Int32 size = arrayType->GetArraySize( arrayData );
			RETURN_INT( size );
		}
	}
	else
	{
		stack.Step( context, nullptr, nullptr );
		RETURN_INT( -1 );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayContains( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate space for element
		const rtti::IType* innerType = arrayType->ArrayGetInnerType();
		void* elementData = RED_ALLOCA( innerType->GetSize() );
		innerType->Construct( elementData );

		// Read element
		stack.Step( context, elementData, innerType );

		// Find the element in the array
		Int32 elementIndex = -1;
		const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
		for ( Uint32 i = 0; i < arraySize; i++ )
		{
			const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
			if ( innerType->Compare( arrayElementData, elementData, 0 ) )
			{
				elementIndex = i;
				break;				
			}
		}

		// Cleanup temporary
		innerType->Destruct( elementData );

		// Return true if element was removed
		RETURN_BOOL( elementIndex != -1 );
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_BOOL( false );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayContainsFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get the element to remove
		stack.m_lValuePtr = nullptr;
		stack.Step( context, nullptr, nullptr );
		void* elementData = stack.m_lValuePtr;
		if ( elementData )
		{
			// Find the element in the array
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			Int32 elementIndex = -1;
			const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
			for ( Uint32 i = 0; i < arraySize; i++ )
			{
				const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
				if ( innerType->Compare( arrayElementData, elementData, 0 ) )
				{
					elementIndex = i;
					break;				
				}
			}

			// Return true if element was removed
			RETURN_BOOL( elementIndex != -1 );
		}
		else
		{
			// Not found
			RETURN_BOOL( false );
		}
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_BOOL( false );
	}
}

void ArrayCount( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate space for element
		const rtti::IType* innerType = arrayType->ArrayGetInnerType();
		void* elementData = RED_ALLOCA( innerType->GetSize() );
		innerType->Construct( elementData );

		// Read element
		stack.Step( context, elementData, innerType );

		// Find the element in the array
		Int32 count = 0;
		const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
		for ( Uint32 i = 0; i < arraySize; i++ )
		{
			const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
			if ( innerType->Compare( arrayElementData, elementData, 0 ) )
			{
				count++;
			}
		}

		// Cleanup temporary
		innerType->Destruct( elementData );

		// Return true if element was removed
		RETURN_INT( count );
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_INT( 0 );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayCountFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get the element to remove
		stack.m_lValuePtr = nullptr;
		stack.Step( context, nullptr, nullptr );
		void* elementData = stack.m_lValuePtr;
		if ( elementData )
		{
			// Find the element in the array
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			Int32 count = 0;
			const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
			for ( Uint32 i = 0; i < arraySize; i++ )
			{
				const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
				if ( innerType->Compare( arrayElementData, elementData, 0 ) )
				{
					count++;
				}
			}

			// Return true if element was removed
			RETURN_INT( count );
		}
		else
		{
			// Not found
			RETURN_INT( 0 );
		}
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_INT( 0 );
	}
}

void ArrayFindFirst( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate space for element
		const rtti::IType* innerType = arrayType->ArrayGetInnerType();
		void* elementData = RED_ALLOCA( innerType->GetSize() );
		innerType->Construct( elementData );

		// Read element
		stack.Step( context, elementData, innerType );

		// Find the element in the array
		Int32 elementIndex = -1;
		const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
		for ( Uint32 i = 0; i < arraySize; i++ )
		{
			const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
			if ( innerType->Compare( arrayElementData, elementData, 0 ) )
			{
				elementIndex = i;
				break;				
			}
		}

		// Cleanup temporary
		innerType->Destruct( elementData );

		// Return index of the element
		RETURN_INT( elementIndex );
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not found
		RETURN_INT( -1 );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayFindFirstFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get the element to remove
		stack.m_lValuePtr = nullptr;
		stack.Step( context, nullptr, nullptr );
		void* elementData = stack.m_lValuePtr;
		if ( elementData )
		{
			// Find the element in the array
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			Int32 elementIndex = -1;
			const Uint32 arraySize = arrayType->ArrayGetArraySize( arrayData );
			for ( Uint32 i = 0; i < arraySize; i++ )
			{
				const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
				if ( innerType->Compare( arrayElementData, elementData, 0 ) )
				{
					elementIndex = i;
					break;				
				}
			}

			// Return index of element
			RETURN_INT( elementIndex );
		}
		else
		{
			// Not found
			RETURN_INT( -1 );
		}
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_INT( -1 );
	}
}

void ArrayFindLast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Disable copy elision for context
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Allocate space for element
		const rtti::IType* innerType = arrayType->ArrayGetInnerType();
		void* elementData = RED_ALLOCA( innerType->GetSize() );
		innerType->Construct( elementData );

		// Read element
		stack.Step( context, elementData, innerType );

		// Find the element in the array
		Int32 elementIndex = -1;
		const Int32 arraySize = arrayType->ArrayGetArraySize( arrayData );
		for ( Int32 i = arraySize - 1; i >= 0; --i )
		{
			const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
			if ( innerType->Compare( arrayElementData, elementData, 0 ) )
			{
				elementIndex = i;
				break;
			}
		}

		// Cleanup temporary
		innerType->Destruct( elementData );

		// Return index of the element
		RETURN_INT( elementIndex );
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not found
		RETURN_INT( -1 );
	}

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;
}

void ArrayFindLastFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

	// Remove from array
	void* arrayData = stack.m_lValuePtr;
	if ( arrayData )
	{
		// Get the element to remove
		stack.m_lValuePtr = nullptr;
		stack.Step( context, nullptr, nullptr );
		void* elementData = stack.m_lValuePtr;
		if ( elementData )
		{
			// Find the element in the array
			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			Int32 elementIndex = -1;
			const Int32 arraySize = arrayType->ArrayGetArraySize( arrayData );
			for ( Int32 i = arraySize - 1; i >= 0; --i )
			{
				const void* arrayElementData = arrayType->ArrayGetArrayElement( arrayData, i );
				if ( innerType->Compare( arrayElementData, elementData, 0 ) )
				{
					elementIndex = i;
					break;				
				}
			}

			// Return index of element
			RETURN_INT( elementIndex );
		}
		else
		{
			// Not found
			RETURN_INT( -1 );
		}
	}
	else
	{
		// Skip element
		stack.Step( context, nullptr, nullptr );

		// Not removed
		RETURN_INT( -1 );
	}
}

void ArrayLast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );

	// Return the last element
	void* arrayData = stack.m_lValuePtr;
	const Uint32 size = arrayType->ArrayGetArraySize( arrayData );
	if ( size > 0 )
	{
		// Copy value
		if ( result )
		{
			RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();

			const rtti::IType* innerType = arrayType->ArrayGetInnerType();
			const void* elementData = arrayType->ArrayGetArrayElement( arrayData, size-1 );
			RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, innerType ), "Invalid result type" );
			innerType->Copy( result, elementData );
		}
	}
	else
	{
		SCRIPT_RUNTIME_WARN( stack, "Cannot call Last() on an array with no elements"  );
	}
}

void ArrayElement( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	const rtti::IType* innerType = arrayType->ArrayGetInnerType();

	// Disable copy elision
	Bool canElideCopy = stack.m_canElideCopy;
	stack.m_canElideCopy = false;

	// Get the pointer to array
	stack.m_lValuePtr = nullptr;
	stack.Step( context, nullptr, nullptr );
	void* arrayData = stack.m_lValuePtr;

	// Evaluate index
	Int32 index = 0;
	stack.Step( context, &index, script::GetRTTIType< Int32 >() );

	// Restore copy elision for rest of the statement
	stack.m_canElideCopy = canElideCopy;

	// Reset context
	stack.m_lValuePtr = nullptr;
	stack.m_lValueType = innerType;

	// Get the index of element to erase
	if ( arrayData )
	{
		// Check that range is valid
		const Int32 arraySize = arrayType->ArrayGetArraySize( arrayData );
		if ( index < 0 || index >= arraySize )
		{
			// Info
			SCRIPT_RUNTIME_WARN_ONCE( stack, "Index %i is out of array bounds. Size = %i", index, arraySize );

			// Clear the result
			ResetValue( innerType, result );
		}
		else
		{
			// Write to result
			void* elementData = arrayType->ArrayGetArrayElement( arrayData, index );
			stack.m_lValuePtr = elementData;

			// Write result
			if ( result )
			{
				RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY();
				RED_FATAL_ASSERT( Helper::ValidateResultType( resultType, innerType ), "Invalid result type" );
				innerType->Copy( result, elementData );
			}
		}
	}
	else
	{
		// Clear the result
		ResetValue( innerType, result );
	}
}

} // script