/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// First parameter denotes the type of array which the function can be called for:
// - IBaseArrayType - both dynamic and static array
// - ArrayType - dynamic array only
// - NativeArrayType - static array only
// "Fast" implementation base on the fact that we don't need to create/destroy
// copy of evaluated argument.
// The only exception if EraseFast, which is "fast" because it replaces erased
// element by the last one, thus reorders the array.

class IScriptable;
class CScriptStackFrame;

namespace rtti
{
	class IBaseArrayType;
	class ArrayType;
	class IType;
}

namespace script
{
	void ArraySize( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayPushBack( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayPopBack( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayInsert( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayRemove( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayRemoveFast( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayErase( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayEraseFast( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayClear( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayResize( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayGrow( const rtti::ArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayContains( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayContainsFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayCount( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayCountFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayFindFirst( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayFindFirstFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayFindLast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayFindLastFast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayLast( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	void ArrayElement( const rtti::IBaseArrayType* arrayType, IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

} // script