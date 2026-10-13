/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"

class IScriptable;
class CScriptStackFrame;

namespace rtti
{
	class Function;
}

class RED_REFLECTION_API ScriptableContextLock
{
public:
	ScriptableContextLock( const IScriptable* context, const rtti::Function* function );
	ScriptableContextLock( const IScriptable* context, const Bool shared );
	~ScriptableContextLock();

	RED_INLINE const IScriptable* GetContext() const { return m_context; }
	RED_INLINE Bool IsShared() const { return m_shared || !m_context; }

private:
	void Lock();
	void Unlock();

	const IScriptable* const m_context;
	const Bool m_shared;
};

struct RED_REFLECTION_API ScriptReentrantRWLock
{
	RTTI_DECLARE_TYPE( ScriptReentrantRWLock );

public:
	ScriptReentrantRWLock();

private:
	static void funcAcquire( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcRelease( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	static void funcAcquireShared( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcReleaseShared( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	red::Atomic< Uint32 > m_exclusiveThreadID;
	Int32 m_recursionDepth;
	red::RWLock m_rwLock;
};