/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "scriptableContextLock.h"
#include "scriptable.h"
#include "scriptableThreadSafetyMonitor.h"
#include "rttiClassBuilder.h"
#include "scriptStackFrame.h"
#include "rttiPointerTypesImpl.h"

RTTI_BEGIN_TYPE( ScriptReentrantRWLock );
	RTTI_SCRIPT_ALIAS( "RWLock" );
	RTTI_IMPORT_ONLY();
	RTTI_NATIVE_STATIC_FUNCTION( "Acquire", funcAcquire );
	RTTI_NATIVE_STATIC_FUNCTION( "Release", funcRelease );
	RTTI_NATIVE_STATIC_FUNCTION( "AcquireShared", funcAcquireShared );
	RTTI_NATIVE_STATIC_FUNCTION( "ReleaseShared", funcReleaseShared );
RTTI_END_TYPE();

ScriptableContextLock::ScriptableContextLock( const IScriptable* context, const rtti::Function* function )
	: m_context( context )
	, m_shared( function->IsConst() || function->IsStatic() )
{
	Lock();
}

ScriptableContextLock::ScriptableContextLock( const IScriptable* context, const Bool shared )
	: m_context( context )
	, m_shared( shared )
{
	Lock();
}

ScriptableContextLock::~ScriptableContextLock()
{
	Unlock();
}

void ScriptableContextLock::Lock()
{
	if ( m_context && !m_shared )
	{
		m_context->AcquireExclusiveLock();
	}
}

void ScriptableContextLock::Unlock()
{
	if ( m_context && !m_shared )
	{
		m_context->ReleaseExclusiveLock();
	}
}

ScriptReentrantRWLock::ScriptReentrantRWLock()
	: m_exclusiveThreadID( 0 )
	, m_recursionDepth( 0 )
{}

void ScriptReentrantRWLock::funcAcquire( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_SCRIPT_REF( ScriptReentrantRWLock, self );
	FINISH_PARAMETERS;

	const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
	const Uint32 snapshotTID = self.m_exclusiveThreadID.GetValue();

	// What matters is we don't try to lock reentrantly; a thread can't race against itself.
	// Doesn't support upgrading to exclusive from shared lock!
	if ( snapshotTID == tid )
	{
		self.m_recursionDepth += 1;
	}
	else
	{
		self.m_rwLock.Acquire();
		self.m_exclusiveThreadID.SetValue( tid );
	}
}

void ScriptReentrantRWLock::funcRelease( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_SCRIPT_REF( ScriptReentrantRWLock, self );
	FINISH_PARAMETERS;

	RED_FATAL_ASSERT( red::ThreadId::CurrentThread().AsNumber() == self.m_exclusiveThreadID.GetValue(), "Release on unowned lock!" );
	if ( self.m_recursionDepth > 0 )
	{
		self.m_recursionDepth -= 1;
	}
	else
	{
		self.m_exclusiveThreadID.SetValue( 0 );
		self.m_rwLock.Release();
	}
}

void ScriptReentrantRWLock::funcAcquireShared( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_SCRIPT_REF( ScriptReentrantRWLock, self );
	FINISH_PARAMETERS;

	// Check if exclusively locked by this thread already, can't race against thread-self; doesn't support reentrant shared lock
	const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
	const Uint32 snapshotTID = self.m_exclusiveThreadID.GetValue();
	if ( snapshotTID != tid )
	{
		self.m_rwLock.AcquireShared();
	}
}

void ScriptReentrantRWLock::funcReleaseShared( IScriptable* context, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER_SCRIPT_REF( ScriptReentrantRWLock, self );
	FINISH_PARAMETERS;

	// Check if exclusively locked by this thread already, can't race against thread-self; doesn't support reentrant shared lock
	const Uint32 tid = red::ThreadId::CurrentThread().AsNumber();
	const Uint32 snapshotTID = self.m_exclusiveThreadID.GetValue();
	if ( snapshotTID != tid )
	{
		self.m_rwLock.ReleaseShared();
	}
}