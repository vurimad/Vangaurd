/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptingSystem.h"
#include "scriptNativeFunctionMap.h"
#include "scriptable.h"


Uint32 CScriptNativeFunctionMap::s_nextFunctionIndex = 0;
TNativeFunc CScriptNativeFunctionMap::s_nativeFunctions[c_maxNativeFunctions] = {};
TNativeGlobalFunc CScriptNativeFunctionMap::s_nativeGlobals[c_maxNativeFunctions] = {};

void CScriptNativeFunctionMap::SetOpcode( const Uint8 opcode, TNativeGlobalFunc function )
{
	RED_FATAL_ASSERT( nullptr == s_nativeGlobals[ opcode ], "Opcode already registered" );
	s_nativeGlobals[ opcode ] = function;
}

Uint32 CScriptNativeFunctionMap::RegisterClassNative( TNativeFunc function )
{
	RED_FATAL_ASSERT( function, "Registering a NULL function" );

	const Uint32 nextIndex = 255 + s_nextFunctionIndex++;
	RED_FATAL_ASSERT( nextIndex < RED_ARRAY_COUNT_U32(s_nativeFunctions), "Native function table is to small" );

	s_nativeFunctions[ nextIndex ] = function;
	return nextIndex;
}

Uint32 CScriptNativeFunctionMap::RegisterGlobalNative( TNativeGlobalFunc function )
{
	RED_FATAL_ASSERT( function, "Registering a NULL function" );

	const Uint32 nextIndex = 255 + s_nextFunctionIndex++;
	RED_FATAL_ASSERT( nextIndex < RED_ARRAY_COUNT_U32(s_nativeFunctions), "Native function table is to small" );

	s_nativeGlobals[ nextIndex ] = function;
	return nextIndex;
}
