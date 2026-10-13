/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "scriptingSystem.h"

#include "../../redContainers/include/map.h"
#include "../../commChannel/include/protoChannel.h"

#include "handle.h"

class CScriptFile;

namespace script
{
	enum class BreakAction : Uint8
	{
		Breakpoint,	// Stop when we encounter a breakpoint
		StepOver,
		StepInto,
		StepOut,
		Next,		// Stop as soon as we can

		Count
	};

	enum BreakpointLockState
	{
		Locked,
		Unlocking,
		Unlocked
	};

	class RED_REFLECTION_API BreakpointResult;
}

/// Scripting system
class RED_REFLECTION_API CScriptingSystem final : public IScriptingSystem, public comm::IProtoMessageListener
{
public:
	typedef red::DynArray< THandle< CResource > > TScriptResourceArray;
	typedef red::Atomic< script::BreakpointLockState > BreakpointLockState;
	typedef red::Map< Uint32, CScriptFile* > TScriptFiles;

public:
	CScriptingSystem();
	~CScriptingSystem();

	//! Unload all loaded scripts
	virtual void UnloadScripts() override;

	//! Load compiled scripts, will do nothing if the new scripts are not valid
	virtual Bool LoadScripts( const red::AbsolutePath& compiledBlobPath, class IScriptDataErrorReporter& err ) override;

	//! Validate compiled scripts header
	virtual Bool ValidateHeader( const red::AbsolutePath& compiledBlobPath ) override;

	virtual Bool InitializeChannel( comm::ChannelFactory& factory ) override;
	virtual void DeinitializeChannel() override;

	//! Disable all breakpoints
	virtual void DisableAllBreakpoints() override;

	virtual void DebugBreakpoint( script::BreakpointType type, IScriptable* context, CScriptStackFrame& stack ) override;
	virtual Bool IsProcessingBreakpoint() override;

	virtual void SetProfilerMode( script::ProfilerMode mode ) override;
	virtual script::ProfilerMode GetProfilerMode() const override;

	//! Call global exec function, usually used from console
	virtual Bool CallGlobalExecFunction( const String& functionAndParams, Bool silentErrors ) override;

	//! Call local object function
	virtual Bool CallLocalFunction( IScriptable* context, const String& functionAndParams, Bool silentErrors ) override;

	//! Call when a thread starts executing a script function
	virtual void ThreadStart() override;

	//! Call when a thread stops executing a script function
	virtual void ThreadEnd() override;

	//! Register function to be called after reloading scripts
	virtual Int32 RegisterOnScriptsLoaded( const script::OnScriptReloadCallback& func ) override;
	virtual void UnregisterOnScriptsLoaded( Int32 id ) override;

	virtual Float GetLastScriptFrameDuration() const override;
	virtual void UpdateLastScriptFrameDuration( Uint64 ticks ) override;
	virtual void ResetLastScriptFrameDuration() override;

	// Creates new script file and adds it to list
	CScriptFile* CreateScriptFile( const Uint32 index );

	// Returns a script file at given index
	const CScriptFile* const GetScriptFile( Uint32 index ) const;

	CScriptFile* FindScriptFile( red::StringView path );

private:
	virtual bool OnMessage( const comm::MessageSharedPtr& message, comm::IProtoResponse& response ) override final;

	script::BreakpointResult ToggleBreakpoint( const red::StringView path, Uint32 position, Bool state );

	// Inform the script editor that these breakpoints are currently not valid
	// Usually we will do this due to a runtime-compilation, and then proceed to re-bind them post-compilation
	void UnbindBreakpoints();

	void HandleBreakpointCondition( script::BreakpointType& type, const CScriptStackFrame& frame );
	Bool ShouldThisThreadBreak( script::BreakpointType type, const CScriptStackFrame& stack ) const;
	Bool LockBreakpoint( script::BreakpointType type, CScriptStackFrame& stack );
	void UnlockBreakpoint( red::ArraySpan< script::ThreadState >& stateSnapshot );
	Bool IsOtherThreadInState( red::ArraySpan< script::ThreadState >& stateSnapshot, const script::ThreadState stateToCheck );
	void WaitForOtherThreadsToStop( red::ArraySpan< script::ThreadState >& stateSnapshot );
	void WaitForOtherThreadsToResume( red::ArraySpan< script::ThreadState >& stateSnapshot );
	void WaitForBreakpointToRelease( CScriptStackFrame& stack );
	void StoreCurrentThreadData( CScriptStackFrame& stack );
	void ReleaseCurrentThreadData();

	void CallInitScriptsFunction();

	TScriptResourceArray m_resources; //!< Resources loaded from script
	BreakpointLockState m_breakpointLockState;
	script::ProfilerMode m_profilerMode;
	red::Atomic< Uint32 > m_threadDataIndexPool;
	TScriptFiles m_scriptFiles; //!< List of script source files
	Uint32 m_breakForThreadId;

	script::BreakAction m_breakAction;
	Uint16 m_currentStackLevel;

	red::Atomic< Uint64 > m_lastScriptFrameDuration;

	static Int32 s_scriptLoadedListenerId;
	red::HashMap < Int32, script::OnScriptReloadCallback > m_scriptLoadedListener;
	red::RWSpinLock	m_scriptLoadedListenerLock;

	red::UniquePtr< comm::IProtoChannel >	m_channel;
	void DeleteRuntimeBreakpoints();
	void DeleteProfileData();
};
