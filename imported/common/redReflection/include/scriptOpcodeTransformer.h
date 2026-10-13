/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

/// Process the opcode stream
/// Can be used to load/save opcodes to a stable medium but also to do dumps, etc
class CScriptOpcodeTransformer
{
public:
	CScriptOpcodeTransformer();
	virtual ~CScriptOpcodeTransformer();

	// end of stream ?
	virtual Bool EndOfStream() const = 0;

	// we are expecting opcode at this location, return the opcode for processing
	virtual Uint8 ProcessOpcode() = 0;

	// process breakpoint
	virtual void ProcessBreakpoint() = 0;

	// process profiler entry point
	virtual void ProcessStartProfile() = 0;

	// process int8
	virtual void ProcessInt8() = 0;

	// process int16
	virtual void ProcessInt16() = 0;

	// process int32
	virtual void ProcessInt32() = 0;

	// process int64
	virtual void ProcessInt64() = 0;

	// process uint8
	virtual void ProcessUint8() = 0;

	// process uint16
	virtual void ProcessUint16() = 0;

	// process uint32
	virtual void ProcessUint32() = 0;

	// process uint64
	virtual void ProcessUint64() = 0;

	// process label offset
	virtual void ProcessLabel() = 0;

	// process floating point value
	virtual void ProcessFloat() = 0;

	// process double precision floating point value
	virtual void ProcessDouble() = 0;

	// process string data
	virtual void ProcessString() = 0;

	virtual void ProcessStaticString() = 0;

	// process name reference (id <-> CName)
	virtual void ProcessName() = 0;

	// process TweakDBID
	virtual void ProcessTweakDBID() = 0;

	// process ResRef
	virtual void ProcessResRef() = 0;

	// process type reference (TypeRef <-> const rtti::IType)
	virtual void ProcessTypeRef() = 0;

	// process enum ( enum <-> const rtti::IType)
	virtual void ProcessEnum() = 0;

	// process object pointer ( pointer <-> const rtti::IType)
	virtual void ProcessPointer() = 0;

	// process property reference (IScriptDataObject <-> const rtti::Property)
	virtual void ProcessPropertyRef() = 0;

	// process function local property reference (IScriptDataObject <-> const rtti::Property)
	virtual void ProcessLocalPropertyRef() = 0;

	// process function local property reference (IScriptDataObject <-> const rtti::Property)
	virtual void ProcessParamPropertyRef() = 0;

	// process function reference (CScriptedDataFunction <-> const rtti::Function)
	virtual void ProcessFunctionRef() = 0;

	// process named value reference
	virtual void ProcessNamedValueRef() = 0;

public:
	// Transform code from one representation to the other
	void PerformTransform();
};