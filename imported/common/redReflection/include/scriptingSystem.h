/**
* Copyright (c) 2007-19 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "profilerMode.h"
#include "../../redMemory/include/function.h"

class IScriptable;
class CScriptStackFrame;

namespace red { class AbsolutePath; }

namespace comm
{
	class ChannelFactory;
}

namespace script
{
	enum class BreakpointType : Uint8
	{
		Breakpoint,
		Step,
		FunctionEntry,
		FunctionExit,

		Count,

		// After the count because it's not a standard breakpoint type
		BreakpointConditionFailed
	};

	enum class ThreadState : Uint8
	{
		NotRunning,
		Running,
		Waiting,
	};

	using OnScriptReloadCallback = red::FixedSizeFunction< void() >;
}

/// Scripting system interface
class RED_REFLECTION_API IScriptingSystem
{
public:
	// get instance of the scripting system 
	static IScriptingSystem& GetInstance();

	//! Unload scripts, should be called before application exists
	virtual void UnloadScripts() = 0;

	//! Load scripts from compiled blob, will do nothing if the new scripts are not valid
	//! By default we will look in "game\cache\scripts.redscripts"
	virtual Bool LoadScripts( const red::AbsolutePath& compiledBlobPath, class IScriptDataErrorReporter& err ) = 0;

	//! Validate compiled scripts header
	virtual Bool ValidateHeader( const red::AbsolutePath& compiledBlobPath ) = 0;

	//! Enables debugging (New system)
	virtual Bool InitializeChannel( comm::ChannelFactory& factory ) = 0;
	virtual void DeinitializeChannel() = 0;

	//! Disable all breakpoints
	virtual void DisableAllBreakpoints() = 0;

	virtual void DebugBreakpoint( script::BreakpointType type, IScriptable* context, CScriptStackFrame& stack ) = 0;
	virtual Bool IsProcessingBreakpoint() = 0;

	virtual void SetProfilerMode( script::ProfilerMode mode ) = 0;
	virtual script::ProfilerMode GetProfilerMode() const = 0;

	//! Call global exec function, usually used from console
	virtual Bool CallGlobalExecFunction( const String& functionAndParams, Bool silentErrors ) = 0;

	//! Call local object function
	virtual Bool CallLocalFunction( IScriptable* context, const String& functionAndParams, Bool silentErrors ) = 0;

	//! Call when a thread starts executing a script function
	virtual void ThreadStart() = 0;

	//! Call when a thread stops executing a script function
	virtual void ThreadEnd() = 0;

	//! Register function to be called after reloading scripts
	virtual Int32 RegisterOnScriptsLoaded( const script::OnScriptReloadCallback& func ) = 0;
	virtual void UnregisterOnScriptsLoaded( Int32 id ) = 0;

	virtual Float GetLastScriptFrameDuration() const = 0;
	virtual void UpdateLastScriptFrameDuration( Uint64 ticks ) = 0;
	virtual void ResetLastScriptFrameDuration() = 0;

protected:
	IScriptingSystem() = default;
	virtual ~IScriptingSystem() = default;
};
