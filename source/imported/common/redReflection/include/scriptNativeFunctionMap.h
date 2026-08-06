/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiCommon.h"
#include "scriptable.h"

/// Mapping of native functions to opcodes
class RED_REFLECTION_API CScriptNativeFunctionMap
{
public:
	//! Set function ( opcodes )
	static void SetOpcode( const Uint8 opcode, TNativeGlobalFunc function );

	//! Register native function
	static Uint32 RegisterClassNative( TNativeFunc function );

	//! Register native global function
	static Uint32 RegisterGlobalNative( TNativeGlobalFunc function );

	//! Get class native function
	static TNativeFunc GetClassNativeFunction( Uint32 index );

	//! Get global native function
	static TNativeGlobalFunc GetGlobalNativeFunction( Uint32 index );

private:

	constexpr static Uint32 c_maxNativeFunctions = 16384;

	static Uint32 s_nextFunctionIndex;
	static TNativeFunc s_nativeFunctions[c_maxNativeFunctions];
	static TNativeGlobalFunc s_nativeGlobals[c_maxNativeFunctions];
};

RED_INLINE TNativeFunc CScriptNativeFunctionMap::GetClassNativeFunction( Uint32 index )
{
	return s_nativeFunctions[index];
}

RED_INLINE TNativeGlobalFunc CScriptNativeFunctionMap::GetGlobalNativeFunction( Uint32 index )
{
	return s_nativeGlobals[index];
}

