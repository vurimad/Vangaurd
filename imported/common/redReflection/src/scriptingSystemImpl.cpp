/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "scriptingSystemImpl.h"

#include "../../redSystem/include/threads.h"
#include "../../commChannel/include/channelConnectionImpl.h"
#include "../../commChannel/include/channelFactory.h"
#include "../../redContainers/include/fundamentalStringParser.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redConfig/include/configVar.h"
#include "../../redSystem/include/processUtility.h"

#include "scriptDataEnvironment.h"
#include "scriptDataValidator.h"
#include "scriptDataBinder.h"
#include "scriptSnapshot.h"
#include "scriptable.h"
#include "scriptFile.h"
#include "scriptingSystem.h"
#include "scriptDebugger.h"
#include "scriptStackFrame.h"
#include "scriptDebuggerData.h"
#include "rttiSystem.h"

//////////////////////////////////////////////////////////////////////////
// usings
using red::String;
using red::DynArray;


//////////////////////////////////////////////////////////////////////////
// Config
namespace Config
{
	TConfigVar< Int32 > cvPort( "Scripts", "Port", 37060, 0, std::numeric_limits<Uint16>::max(), eConsoleVarFlag_Developer );
	TConfigVar< Int32 > cvPortRange( "Scripts", "PortRange", 10, 0, std::numeric_limits<Uint16>::max(), eConsoleVarFlag_Developer );
}

//////////////////////////////////////////////////////////////////////////
// Script Thread Management
#ifdef RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION
static const Uint32 MaxScriptThreadDataInstances = 28;
#else
static const Uint32 MaxScriptThreadDataInstances = 12;
#endif

static red::StaticArray< script::Thread, MaxScriptThreadDataInstances > g_ScriptThreadData;
static red::StaticArray< script::ThreadState, MaxScriptThreadDataInstances > g_ScriptThreadStates;
RED_TLS static Uint32 g_scriptThreadDataIndex = MaxScriptThreadDataInstances + 1;

Int32 CScriptingSystem::s_scriptLoadedListenerId = 0;

//////////////////////////////////////////////////////////////////////////
// CScriptingSystem
CScriptingSystem::CScriptingSystem()
	: m_resources( red::PoolScript() )
	, m_breakpointLockState( script::BreakpointLockState::Unlocked )
	, m_profilerMode( script::ProfilerMode_Off )
	, m_threadDataIndexPool( 0 )
	, m_scriptFiles( red::PoolScript() )
	, m_breakForThreadId( 0 )
	, m_breakAction( script::BreakAction::Breakpoint )
	, m_currentStackLevel( 0 )
	, m_lastScriptFrameDuration( 0 )
	, m_scriptLoadedListener( red::PoolScript() )
{
	// Initialize opcodes
	extern void ExportCoreOpcodes();
	ExportCoreOpcodes();

	g_ScriptThreadStates.Resize( MaxScriptThreadDataInstances );
	g_ScriptThreadData.Resize( MaxScriptThreadDataInstances );

	red::Memset( g_ScriptThreadStates.Data(), static_cast< Uint32 >( script::ThreadState::NotRunning ), sizeof( script::ThreadState ) * MaxScriptThreadDataInstances );
}

CScriptingSystem::~CScriptingSystem()
{}

void CScriptingSystem::UnloadScripts()
{
	DeleteProfileData();
}

Bool CScriptingSystem::LoadScripts( const red::AbsolutePath& compiledBlobPath, IScriptDataErrorReporter& err )
{
	RED_LOG_INFO( "[Scripts] Start loading script blob %s", compiledBlobPath.ToDebugString() );

	// Load the script environment from the specified file (NOTE: absolute path)
	CScriptedDataEnvironment env;
	if ( !env.Load( compiledBlobPath ) )
	{
		RED_LOG_ERROR( "[Scripts] Failed to load script blob %s", compiledBlobPath.ToDebugString() );
		err.ValidationError( "Failed to load scripts" );
		return false;
	}

	// Validate the scripts for current RTTI
	CScriptDataValidator validator;
	if ( !validator.Validate( env, err ) )
	{
		RED_LOG_ERROR( "[Scripts] Failed to validate script blob %s", compiledBlobPath.ToDebugString() );
		return false;
	}

	UnbindBreakpoints();
	DeleteRuntimeBreakpoints();

	DeleteProfileData();

	// Get all scriptable objects in the system
	DynArray< THandle< IScriptable > > allScriptables{ red::PoolScript() };
	IScriptable::CollectAllScriptableObjects( allScriptables );

	// Build script data snapshot
	CScriptSnapshot snapshot;
	snapshot.CaptureScriptData( allScriptables );

	// Destroy all script related data in current runtime classes (we hope that all important stuff is stored in the snapshot by now)
	for ( Uint32 i = 0; i < allScriptables.Size(); ++i )
	{
		IScriptable* scriptable = allScriptables[ i ].Get();
		if ( scriptable )
		{
			scriptable->ReleaseScriptPropertiesBuffer();
		}
	}

	GetRttiSystem().ClearScriptedGlobalFunctions();

	// Bind loaded scripts with our RTTI
	{
		auto fileMapper = [ this ]( const Uint32 index ) -> CScriptFile*
		{
			return CreateScriptFile( index );
		};

		CScriptDataBinder binder( GetRttiSystem(), fileMapper );
		if ( !binder.BindScripts( env, err ) )
		{
			RED_LOG_ERROR( "[Scripts] Failed to bind script blob %s", compiledBlobPath.ToDebugString() );
			return false;
		}
	}

	// Since we got this far restore RTTI data
	for ( Uint32 i = 0; i < allScriptables.Size(); ++i )
	{
		IScriptable* scriptable = allScriptables[ i ].Get();
		if ( scriptable )
		{
			scriptable->CreateScriptPropertiesBuffer();
		}
	}

	RED_LOG_INFO( "[Scripts] Stop loading script blob %s", compiledBlobPath.ToDebugString() );

	// Restore script snapshot
	snapshot.RestoreScriptData();

	// Notify all scripted objects
	for ( Uint32 i = 0; i < allScriptables.Size(); ++i )
	{
		IScriptable* scriptable = allScriptables[ i ].Get();
		if ( scriptable )
		{
			scriptable->OnScriptReloaded();
		}
	}

	if ( m_channel.Get() )
	{
		comm::ScriptBinaryReloaded reloadedMessage;
		m_channel->Send( reloadedMessage );
	}

	// Notify all listeners
	{
		RED_SCOPE_SHARED_LOCK( m_scriptLoadedListenerLock );
		for ( const auto& listener : m_scriptLoadedListener )
		{
			const auto& func = listener.Value();
			if ( func )
			{
				func();
			}
		}
	}

	// Call script init function
	CallInitScriptsFunction();

	// Reloaded
	return true;
}

Bool CScriptingSystem::ValidateHeader( const red::AbsolutePath& compiledBlobPath )
{
	CScriptedDataEnvironment env;
	return env.ValidateHeader( compiledBlobPath );
}

bool CScriptingSystem::InitializeChannel( comm::ChannelFactory& factory )
{
	RED_FATAL_ASSERT( !m_channel, "Network already initialised" );

	using namespace red::Network::Utils;

	const Uint16 initialPort = Config::cvPort.Get();
	const Uint16 portRange = Config::cvPortRange.Get();
	Uint16 portIncrement = 0;

	Uint16 port = initialPort;
	while ( !m_channel && portIncrement < portRange )
	{
		port = initialPort + portIncrement;
		m_channel = factory.CreateListenerProtoChannel( "scripts", CommAddress( "RedScriptsHost", port ) );

		++portIncrement;
	}

	if ( m_channel )
	{
		m_channel->RegisterListener( this );
		RED_LOG_INFO( "[Scripts] Created scripts listener at port: %u", port );
		return true;
	}

	RED_LOG_INFO( "[Scripts] Failed to create scripts listener" );
	return false;
}

void CScriptingSystem::DeinitializeChannel()
{
	if ( m_channel )
	{
		m_channel->UnregisterListener( this );
		m_channel.Reset();
	}
}

CScriptFile* CScriptingSystem::CreateScriptFile(const Uint32 index)
{
	CScriptFile* newScriptFile = m_scriptFiles[index];
	if (!newScriptFile)
	{
		newScriptFile = RED_NEW( CScriptFile );
		m_scriptFiles[index] = newScriptFile;
	}

	return newScriptFile;
}

const CScriptFile* const CScriptingSystem::GetScriptFile( Uint32 index ) const
{
	CScriptFile* file = nullptr;
	if ( !m_scriptFiles.Find( index, file ) )
	{
		RED_LOG_ERROR( "Scripts: Unknown script file with index %d (%d files registered)", index, m_scriptFiles.Size() );
	}

	return file;
}

CScriptFile* CScriptingSystem::FindScriptFile( red::StringView path )
{
	const Uint32 hash = red::CalculatePathHash32( path.Data(), path.Length() );

	for ( const auto& pair : m_scriptFiles )
	{
		if( pair.Value()->GetHashedPath() == hash )
			return pair.Value();
	}

	return nullptr;
}

void CScriptingSystem::DisableAllBreakpoints()
{
	for ( auto scriptFile : m_scriptFiles )
	{
		scriptFile.Value()->DisableAllBreakpoints();
	}
}

Bool CScriptingSystem::ShouldThisThreadBreak( script::BreakpointType type, const CScriptStackFrame& stack ) const
{
#ifndef NO_SCRIPT_DEBUG

	constexpr static Uint32 c_actionCount = static_cast<Uint32>( script::BreakAction::Count );
	constexpr static Uint32 c_typeCount = static_cast<Uint32>( script::BreakpointType::Count );

	constexpr static bool c_breakTable[ c_actionCount ][ c_typeCount ] =
	{
		// action \ type:  Breakpoint    | Step          | FunctionEntry | FunctionExit
		/* Breakpoint */ { true,           false,          false,          false },
		/* StepOver */   { true,           true,           false,          false },
		/* StepInto */   { true,           true,           true,           true },
		/* StepOut */    { true,           false,          false,          true },
		/* Next */       { true,           true,           true,           true }
	};

	if ( !stack.m_isDebugging )
	{
		return false;
	}

	const bool situationMatches = c_breakTable[ (Uint8)m_breakAction ][ (Uint8)type ];

	bool levelMatches = true;
	if ( type != script::BreakpointType::Breakpoint )
	{
		switch ( m_breakAction )
		{
		case script::BreakAction::StepOver:
			levelMatches = m_currentStackLevel >= stack.GetLevel();
			break;

		case script::BreakAction::StepOut:
			levelMatches = m_currentStackLevel > stack.GetLevel();
			break;
		}
	}

	// Stop if we've hit a breakpoint, or if we're stepping over code for a specific thread
	const Bool breakOnThisThread = type == script::BreakpointType::Breakpoint || m_breakForThreadId == 0 || m_breakForThreadId == red::ThreadId::CurrentThread().AsNumber();

	return situationMatches && levelMatches && breakOnThisThread;

#else
	return false;
#endif
}

Bool CScriptingSystem::LockBreakpoint( script::BreakpointType type, CScriptStackFrame& stack )
{
	while( true )
	{
		// Re-evaluate whether or not this thread should break before attempting a lock, as other threads can change the situation in the mean time
		if( !ShouldThisThreadBreak( type, stack ) )
			return false;

		if( m_breakpointLockState.CompareExchange( script::BreakpointLockState::Locked, script::BreakpointLockState::Unlocked ) == script::BreakpointLockState::Unlocked )
			return true;
		else
			// Another thread beat us to the breakpoint that this thread also wanted
			WaitForBreakpointToRelease( stack );
	}
}

void CScriptingSystem::UnlockBreakpoint( red::ArraySpan< script::ThreadState >& stateSnapshot )
{
	// When we release the other threads, we use this secondary temporary "lock" to
	// know that all the data has been cleaned up and that it's safe to proceed
	m_breakpointLockState.SetValue( script::BreakpointLockState::Unlocking );

	// Tell breakpoints to resume
	WaitForOtherThreadsToResume( stateSnapshot );

	// They're all ready to go, let's resume normal processing
	m_breakpointLockState.SetValue( script::BreakpointLockState::Unlocked );
}

red::StaticArray< script::ThreadState, MaxScriptThreadDataInstances > CreateSnapshot()
{
	red::StaticArray< script::ThreadState, MaxScriptThreadDataInstances > threadStateSnapshot;
	threadStateSnapshot.Resize( MaxScriptThreadDataInstances );
	red::Memcpy( threadStateSnapshot.Data(), g_ScriptThreadStates.Data(), sizeof( script::ThreadState ) * MaxScriptThreadDataInstances );
	return threadStateSnapshot;
}

red::StaticArray< script::Thread, MaxScriptThreadDataInstances > CreateContiguousDataForDebugger( const red::StaticArray< script::ThreadState, MaxScriptThreadDataInstances >& threadStateSnapshot )
{
	red::StaticArray< script::Thread, MaxScriptThreadDataInstances > contiguousThreadData;

	for ( Uint32 i = 0; i < MaxScriptThreadDataInstances; ++i )
	{
		// Any threads that weren't running before we took our snapshot are ignored
		const Bool copyThisThread = threadStateSnapshot[ i ] != script::ThreadState::NotRunning;
		const Bool threadCanBeCopied = g_ScriptThreadStates[ i ] == script::ThreadState::Waiting;
		if ( copyThisThread && threadCanBeCopied )
		{
			contiguousThreadData.PushBack( g_ScriptThreadData[ i ] );
		}
	}

	return contiguousThreadData;
}

Bool CScriptingSystem::IsOtherThreadInState( red::ArraySpan< script::ThreadState >& stateSnapshot, const script::ThreadState stateToCheck )
{
	for ( Uint32 i = 0; i < stateSnapshot.Size(); ++i )
	{
		// Any threads that weren't running before we took our snapshot are ignored
		const Bool checkThisThread = stateSnapshot[ i ] != script::ThreadState::NotRunning;

		if ( checkThisThread )
		{
			const Bool threadIsInState = g_ScriptThreadStates[ i ] == stateToCheck;

			if ( threadIsInState )
				return true;
		}
	}

	return false;
}

void CScriptingSystem::WaitForOtherThreadsToStop( red::ArraySpan< script::ThreadState >& stateSnapshot )
{
	while ( IsOtherThreadInState( stateSnapshot, script::ThreadState::Running ) )
		red::SleepOnCurrentThread( 1 );
}

void CScriptingSystem::WaitForOtherThreadsToResume( red::ArraySpan< script::ThreadState >& stateSnapshot )
{
	while ( IsOtherThreadInState( stateSnapshot, script::ThreadState::Waiting ) )
		red::SleepOnCurrentThread( 1 );
}

void CScriptingSystem::WaitForBreakpointToRelease( CScriptStackFrame& stack )
{
	Bool dataStored = false;

	while ( m_breakpointLockState.GetValue() == script::BreakpointLockState::Locked )
	{
		if ( !dataStored )
		{
			RED_LOG( "Script thread halted due to break point on another thread: %u", red::ThreadId::CurrentThread().AsNumber() );

			StoreCurrentThreadData( stack );

			dataStored = true;
		}

		red::SleepOnCurrentThread( 100 );
	}

	ReleaseCurrentThreadData();

	//
	while ( m_breakpointLockState.GetValue() == script::BreakpointLockState::Unlocking )
		continue;
}

void CScriptingSystem::ThreadStart()
{
	// Assign a new breakpoint to this script thread
	if ( g_scriptThreadDataIndex > MaxScriptThreadDataInstances )
	{
		g_scriptThreadDataIndex = m_threadDataIndexPool.PostIncrement();
	}

	if ( g_scriptThreadDataIndex < MaxScriptThreadDataInstances )
	{
		g_ScriptThreadStates[ g_scriptThreadDataIndex ] = script::ThreadState::Running;
	}
	else
	{
		RED_LOG_WARNING( "Could not assign index to script thread state, thread will not be debugged: %u", red::ThreadId::CurrentThread().AsNumber() );
	}
}

void CScriptingSystem::ThreadEnd()
{
	if ( g_scriptThreadDataIndex < MaxScriptThreadDataInstances )
	{
		if ( m_breakForThreadId == red::ThreadId::CurrentThread().AsNumber() )
		{
			if ( m_breakAction == script::BreakAction::StepOver )
			{
				m_breakAction = script::BreakAction::StepOver;
			}
			else
			{
				m_breakAction = script::BreakAction::Breakpoint;
			}

			m_breakForThreadId = 0;
		}

		g_ScriptThreadStates[ g_scriptThreadDataIndex ] = script::ThreadState::NotRunning;
	}
}

Int32 CScriptingSystem::RegisterOnScriptsLoaded( const script::OnScriptReloadCallback& func )
{
	RED_SCOPE_LOCK( m_scriptLoadedListenerLock );
	{
		const Int32 id = s_scriptLoadedListenerId++;
		m_scriptLoadedListener[id] = func;
		return id;
	}
}

void CScriptingSystem::UnregisterOnScriptsLoaded( Int32 id )
{
	RED_SCOPE_LOCK( m_scriptLoadedListenerLock );
	{
		m_scriptLoadedListener[id] = {};
	}
}

void CScriptingSystem::StoreCurrentThreadData( CScriptStackFrame& stack )
{
	if ( g_scriptThreadDataIndex < MaxScriptThreadDataInstances )
	{
		script::Thread& data = g_ScriptThreadData[ g_scriptThreadDataIndex ];

		data.m_id = red::ThreadId::CurrentThread().AsNumber();
		data.SetStack( stack );

		g_ScriptThreadStates[ g_scriptThreadDataIndex ] = script::ThreadState::Waiting;
	}
}

Float CScriptingSystem::GetLastScriptFrameDuration() const
{
	red::Timer timer;
	return static_cast< Float >( m_lastScriptFrameDuration.GetValue() / timer.GetFrequency() * 1000.f );
}

void CScriptingSystem::UpdateLastScriptFrameDuration( Uint64 ticks )
{
	m_lastScriptFrameDuration.ExchangeAdd( ticks );
}

void CScriptingSystem::ResetLastScriptFrameDuration()
{
	m_lastScriptFrameDuration.SetValue( 0 );
}

void CScriptingSystem::ReleaseCurrentThreadData()
{
	if ( g_scriptThreadDataIndex < g_ScriptThreadData.Size() )
	{
		script::Thread& data = g_ScriptThreadData[ g_scriptThreadDataIndex ];

		data.m_id = 0;
		data.m_stack.Resize( 0 );

		g_ScriptThreadStates[ g_scriptThreadDataIndex ] = script::ThreadState::Running;
	}
}

void CScriptingSystem::CallInitScriptsFunction()
{
	auto initializeScriptsFunction = GetRttiSystem().FindGlobalFunction( RED_NAME_CONSTEXPR_NOREG( "InitializeScripts;" ) );
	RED_FATAL_ASSERT( initializeScriptsFunction != nullptr, "Cannot find InitializeScipts function" );
	rtti::FunctionContext_RttiParams ctx( nullptr, nullptr, 0, nullptr, nullptr );
	initializeScriptsFunction->Call( ctx );
}

void CScriptingSystem::DebugBreakpoint( script::BreakpointType type, IScriptable* context, CScriptStackFrame& stack )
{
#ifndef NO_SCRIPT_DEBUG
	using namespace script;

	if( !m_channel.Get() || !m_channel->IsConnected() )
		return;

	// We need to do some handling of script breakpoints 
	HandleBreakpointCondition( type, stack );

	// Wait for the other thread to exit the breakpoint
	WaitForBreakpointToRelease( stack );

	// Lock the breakpoint (if this thread is at a breakpoint) so all other threads will have to wait at the synchronisation points
	if( LockBreakpoint( type, stack ) )
	{
		RED_LOG( "Script thread breakpoint hit: %u", red::ThreadId::CurrentThread().AsNumber() );

		StoreCurrentThreadData( stack );

		// Create a snapshot of the current thread states so we know which threads to wait for data from
		// (All other threads will also stop and wait, but we won't take their data
		auto threadStateSnapshot = CreateSnapshot();
		red::ArraySpan< script::ThreadState > snapshotSpan( threadStateSnapshot );

		// Wait for all the other threads in the snapshot to hit the synchronisation points
		WaitForOtherThreadsToStop( snapshotSpan );

		// Copy the data for each thread into a contiguous array for the debugger
		auto contiguousThreadData = CreateContiguousDataForDebugger( threadStateSnapshot );
		red::ArraySpan< script::Thread > contiguousThreadDataSpan( contiguousThreadData );

		// Create the debugger that will communicate with the script IDE and
		// hold us here until the user continues execution or stops debugging
		m_currentStackLevel = stack.GetLevel();
		RED_FATAL_ASSERT( stack.m_debugData.GetLine() != (Uint16)-1 );
		CScriptDebugger debugger( *m_channel, red::ThreadId::CurrentThread().AsNumber(), contiguousThreadDataSpan, type );
		debugger.ProcessBreakpoint();

		// Clean up the data for this thread
		ReleaseCurrentThreadData();

		// Release the breakpoint, tell the other threads to clean up
		// their data and get ready to start executing again
		UnlockBreakpoint( snapshotSpan );
	}

#endif
}

Bool CScriptingSystem::IsProcessingBreakpoint()
{
	return m_breakpointLockState.GetValue() == script::BreakpointLockState::Locked;
}

void CScriptingSystem::SetProfilerMode( script::ProfilerMode mode )
{
	m_profilerMode = mode;
}

script::ProfilerMode CScriptingSystem::GetProfilerMode() const
{
	return m_profilerMode;
}

script::BreakpointResult CScriptingSystem::ToggleBreakpoint( const red::StringView path, Uint32 position, Bool state )
{
	CScriptFile* file = FindScriptFile( path );

	if( !file )
	{
		return script::BreakpointResult( nullptr );
	}

	return file->SetBreakpoint( position, state );
}

void CScriptingSystem::UnbindBreakpoints()
{
	for ( auto scriptFile : m_scriptFiles )
	{
		auto disabledBreakpoints = scriptFile.Value()->DisableAllBreakpoints();

		for( const script::BreakpointResult& breakpoint : disabledBreakpoints )
		{
			comm::ScriptBreakpointUnbound message;

			message.breakpoint.standard.file = scriptFile.Value()->GetPath();
			message.breakpoint.standard.line = breakpoint.GetLine();
			message.breakpoint.column = breakpoint.GetColumn();
			message.breakpoint.length = breakpoint.GetLength();

			m_channel->Send( message );
		}
	}
}

void CScriptingSystem::DeleteRuntimeBreakpoints()
{
	for ( auto scriptFile : m_scriptFiles )
	{
		scriptFile.Value()->ClearBreakpoints();
	}
}

void CScriptingSystem::DeleteProfileData()
{
#ifdef USE_PROFILER
	for ( auto scriptFile : m_scriptFiles )
	{
		scriptFile.Value()->ClearInstrumentationObjects();
	}
#endif
}

void CScriptingSystem::HandleBreakpointCondition( script::BreakpointType& type, const CScriptStackFrame& frame )
{
#ifndef NO_SCRIPT_DEBUG 

	if( type == script::BreakpointType::BreakpointConditionFailed )
	{
		// If the condition has failed (evaluated as false)
		// then we switch back to a step, which may still happen
		// depending on what the user is doing
		type = script::BreakpointType::Step;

		// If the breakpoint won't break, notify the script editor that
		// a breakpoint was skipped (so that the hitcount is maintained)
		if( !ShouldThisThreadBreak( type, frame ) )
		{
			comm::ScriptBreakpointSkipped message;
			message.breakpoint.standard.file = frame.m_function->GetCode().GetScriptFile()->GetPath();
			message.breakpoint.standard.line = frame.m_debugData.GetLine();
			message.breakpoint.column = frame.m_debugData.GetColumn();
			message.breakpoint.length = frame.m_debugData.GetLength();

			m_channel->Send( message );
		}
	}
#endif
}

// todo:
RED_REFLECTION_API Bool GSkipKeyword( const char*& str, const char* match )
{
	const char* ptrStart = str;
	if ( GParseWhitespaces( str ) )
	{
		const Uint32 matchLen = static_cast< Uint32 >(red::Strlen( match ));
		String tokenSoFar;
		while ( *str > 32 && tokenSoFar.Length() < matchLen )
		{
			char text[] = { *str, 0 };
			tokenSoFar += text;
			str++;

			if ( tokenSoFar.EqualsNC( match ) )
			{
				return true;
			}
		}
	}

	str = ptrStart;
	return false;
}

static Bool GParseStringFunctionParam( const char*& stream, String& outVal )
{
	if ( *stream == '\"' || *stream == '\'' )
	{
		// Try parse as quoted string
		++stream;
		while ( *stream )
		{
			if ( *stream == '\"' || *stream == '\'' )
			{
				++stream;
				break;
			}

			char chars[2] = { *stream, 0 };
			outVal += chars;
			++stream;
		}
		return true;
	}

	return false;
}

RED_REFLECTION_API Bool GParseFunctionParam( const char*& stream, String& token )
{
	String outputVal;
	if ( GParseWhitespaces( stream ) )
	{	
		outputVal.Reserve( 32 );

		if ( !GParseStringFunctionParam( stream, outputVal ) )
		{
			// Grab normal text
			while ( *stream && *stream > ' ' )
			{
				if ( *stream == '(' || *stream == ')' || *stream == ',' )
				{
					break;
				}

				char chars[2] = { *stream, 0 };
				outputVal += chars;
				stream++;				
			}
		}

		if ( outputVal.Length() )
		{
			token = std::move( outputVal );
			return true;
		}
	}

	// Not grabbed
	return false;
}

Bool CScriptingSystem::CallGlobalExecFunction( const String& functionAndParams, Bool silentErrors )
{
	const char* str = functionAndParams.AsChar();

	// Tokenize, get function name
	String functionName;
	if ( !GParseFunctionParam( str, functionName ) )
	{
		if ( !silentErrors )
		{
			RED_LOG( "Script: Error: Unable to parse function name" );
		}
		return false;
	}

	// Find global function with that name
	CName tempshitName = RED_NAME( functionName );
	const rtti::Function* global = GetRttiSystem().FindGlobalFunction( tempshitName );
	if ( !global )
	{
		if ( !silentErrors )
		{
			RED_LOG( "Script: Error: Unknown global function '%hs'", functionName.AsChar() );
		}
		return false;
	}

	// It's not an exec function, we cannot call it unless in debug build
	if ( !global->IsNative() && !global->IsExec() )
	{
		RED_LOG( "Script: Error: No privledges to call script function '%hs'. Add 'exec' keyword.", functionName.AsChar() );
		return false;		
	}

	// Expect a '(' and parameter list
	DynArray< String > parameters{ red::PoolScript() };
	if ( GSkipKeyword( str, "(" ) )
	{
		// Parse parameters
		while ( *str )
		{
			// End of parameters
			if ( GSkipKeyword( str, ")" ) )
			{
				break;
			}

			// Detect to many parameters
			if ( parameters.Size() == global->GetNumParameters() )
			{
				RED_LOG( "Script: Error: To many parameters for function '%hs'.", functionName.AsChar() );
				return false;
			}

			// We should have an ',' separating parameters
			if ( parameters.Size() )
			{
				if ( !GSkipKeyword( str, "," ) )
				{
					RED_LOG( "Script: Error: Parse error: expecting ','" );
					return false;
				}
			}

			// Get the matching function parameter
			const rtti::Property* param = global->GetParameter( parameters.Size() );
			RED_ASSERT( param );

			// Parse token
			String paramValue;
			if ( GParseFunctionParam( str, paramValue ) )
			{
				// Add to value list
				parameters.PushBack( paramValue );
			}
			else
			{
				// If not optional it's an error
				if ( !(param->GetFlags() & PF_FuncOptionaParam) )
				{
					RED_LOG( "Script: Error: unable to parse value for parameter '%hs'", param->GetName().AsChar() );
					return false;
				}

				// No parameter given
				parameters.PushBack( String::EMPTY() );
			}
		}
	}

	// Count non optional parameters
	for ( Uint32 i=parameters.Size(); i<global->GetNumParameters(); i++ )
	{
		// Get the matching function parameter
		const rtti::Property* param = global->GetParameter( i );
		RED_ASSERT( param );

		// If not optional it's an error
		if ( !(param->GetFlags() & PF_FuncOptionaParam) )
		{
			RED_LOG( "Script: Error: value for parameter '%hs' required", param->GetName().AsChar() );
			return false;
		}
	}

	// Call the function
	String resultValue;
	rtti::FunctionContext_StringParams ctx( nullptr, &parameters, &resultValue, nullptr );
	global->Call( ctx );

	// Print result
	if ( global->GetReturnValue() )
	{
		RED_LOG( "Script: Result: '%hs'", resultValue.AsChar() );
	}

	// Function was executed
	return true;
}

Bool CScriptingSystem::CallLocalFunction( IScriptable* context, const String& functionAndParams, Bool silentErrors )
{
	const char* str = functionAndParams.AsChar();

	// Tokenize, get function name
	String functionName;
	if ( !GParseFunctionParam( str, functionName ) )
	{
		if ( !silentErrors )
		{
			RED_LOG( "Script: Unable to parse function name" );
		}
		return false;
	}

	// Find global function with that name
	CName tempshitName = RED_NAME( functionName );
	const rtti::Function* function;

	if ( !FindFunction( context, tempshitName, function ) )
	{	
		if ( !silentErrors )
		{
			RED_LOG( "Script: Unknown local function '%hs'", functionName.AsChar() );
		}
		return false;
	}

	// It's not an exec function, we cannot call it unless in debug build
	if ( !function->IsEvent() && !function->IsExec() )
	{
		RED_LOG( "Script: No privledges to call script function '%hs'.", functionName.AsChar() );
		return false;		
	}

	// Expect a '(' and parameter list
	DynArray< String > parameters{ red::PoolScript() };
	if ( GSkipKeyword( str, "(" ) )
	{
		// Parse parameters
		while ( *str )
		{
			// End of parameters
			if ( GSkipKeyword( str, ")" ) )
			{
				break;
			}

			// Detect to many parameters
			if ( parameters.Size() == function->GetNumParameters() )
			{
				RED_LOG( "Script: To many parameters for function '%hs'.", functionName.AsChar() );
				return false;
			}

			// We should have an ',' separating parameters
			if ( parameters.Size() )
			{
				if ( !GSkipKeyword( str, "," ) )
				{
					RED_LOG( "Script: Parse error: expecting ','" );
					return false;
				}
			}

			// Get the matching function parameter
			const rtti::Property* param = function->GetParameter( parameters.Size() );
			RED_ASSERT( param );

			// Parse token
			String paramValue;
			if ( GParseFunctionParam( str, paramValue ) )
			{
				// Add to value list
				parameters.PushBack( paramValue );
			}
			else
			{
				// If not optional it's an error
				if ( !(param->GetFlags() & PF_FuncOptionaParam) )
				{
					RED_LOG( "Script: Parse error: unable to parse value for parameter '%hs'", param->GetName().AsChar() );
					return false;
				}

				// No parameter given
				parameters.PushBack( String::EMPTY() );
			}
		}
	}

	// Count non optional parameters
	for ( Uint32 i=parameters.Size(); i<function->GetNumParameters(); i++ )
	{
		// Get the matching function parameter
		const rtti::Property* param = function->GetParameter( i );
		RED_ASSERT( param );

		// If not optional it's an error
		if ( !(param->GetFlags() & PF_FuncOptionaParam) )
		{
			RED_LOG( "Script: Parse error: value for parameter '%hs' required", param->GetName().AsChar() );
			return false;
		}
	}

	// Call the function
	String resultValue;
	rtti::FunctionContext_StringParams ctx( nullptr, &parameters, &resultValue, nullptr );
	function->Call( ctx );

	// Print result
	if ( function->GetReturnValue() )
	{
		RED_LOG( "Script: Result: '%hs'", resultValue.AsChar() );
	}

	// Function was executed
	return true;
}

bool CScriptingSystem::OnMessage( const comm::MessageSharedPtr& message, comm::IProtoResponse& response )
{
	if ( message->IsA( comm::EMessageID::ScriptProcessInfoRequest ) )
	{
		comm::ScriptProcessInfo reply;

#if defined( RED_PLATFORM_WINPC )

		const int filenameSize = 256;
		char filename[ filenameSize ];
		GetModuleFileNameA( NULL, filename, filenameSize );

		const int titleSize = 256;
		char title[ titleSize ];

		HWND wnd = NULL;

		auto EnumWindowsProc = []( HWND hwnd, LPARAM lparam )-> BOOL
		{
			HWND* wnd = reinterpret_cast<HWND*>( lparam );

			DWORD windowProcessId;
			GetWindowThreadProcessId( hwnd, &windowProcessId );

			if ( GetCurrentProcessId() == windowProcessId )
			{
				*wnd = hwnd;
				return FALSE;
			}

			return TRUE;
		};

		EnumWindows( EnumWindowsProc, reinterpret_cast<LPARAM>( &wnd ) );

		GetWindowTextA( wnd, title, titleSize );

		const int usernameSize = 256;
		DWORD usernameSizeOut = usernameSize;
		char username[ usernameSize ];

		GetUserNameA( username, &usernameSizeOut );

		reply.processId = GetProcessId();
		reply.exe = filename;
		reply.name = title;
		reply.user = username;
#elif defined( RED_PLATFORM_DURANGO )
		reply.processId = GetProcessId();
		reply.exe = "cpLauncher.exe";
		reply.name = "cpLauncher";
		reply.user = "";
#elif defined( RED_PLATFORM_ORBIS )
		reply.processId = GetProcessId();
		reply.exe = "/app0/cpLauncher.elf";
		reply.name = "cpLauncher";
		reply.user = "";
#else
		reply.name = "Not implemented on platform";
		reply.processId = 12345;
#endif

		response.Send( reply );

		return true;
	}
	else if ( message->IsA( comm::EMessageID::ScriptBreakpointRequest ) )
	{
		const comm::ScriptBreakpointRequest* breakpointMessage = static_cast<comm::ScriptBreakpointRequest*>( message.Get() );

		/*
		TODO: Path normalization doesn't work for now, because script paths are using CFileManager::ALTERNATIVE_DIRECTORY_SEPARATOR.

		// We need to normalize the directory separators, since they may be different if the script editor has connected
		// from another platform (i.e. from windows to orbis
		String normalizedFile( breakpointMessage->breakpoint.standard.file.AsChar() );
		normalizedFile.ReplaceAll( CFileManager::ALTERNATIVE_DIRECTORY_SEPARATOR, CFileManager::DIRECTORY_SEPARATOR );
		*/

		Uint32 line = breakpointMessage->breakpoint.standard.line;
		Bool enabled = breakpointMessage->enabled;

		script::BreakpointResult result = ToggleBreakpoint( breakpointMessage->breakpoint.standard.file.AsChar(), breakpointMessage->breakpoint.position, enabled );

		comm::ScriptBreakpointConfirmation confirmationMessage;
		confirmationMessage.success = result.Success();

		confirmationMessage.request = breakpointMessage->breakpoint;

		if( result.Success() )
		{
			confirmationMessage.confirmed.standard.file = breakpointMessage->breakpoint.standard.file;
			confirmationMessage.confirmed.standard.line = result.GetLine();
			confirmationMessage.confirmed.column = result.GetColumn();
			confirmationMessage.confirmed.length = result.GetLength();
		}

		response.Send( confirmationMessage );
	}
	else if ( message->IsA( comm::EMessageID::ScriptBreakpointCondition ) )
	{
		const comm::ScriptBreakpointCondition* conditionMessage = static_cast< comm::ScriptBreakpointCondition* >( message.Get() );

		CScriptFile* file = FindScriptFile( conditionMessage->breakpoint.standard.file );

		if ( file )
		{
			script::RuntimeBreakpoint* breakpoint = file->FindBreakpoint( conditionMessage->breakpoint.position );

			if ( breakpoint )
				breakpoint->SetCondition( conditionMessage->expression, conditionMessage->style );
		}
	}
	else if( message->IsA( comm::EMessageID::ScriptBreakpointPasscount ) )
	{
		const comm::ScriptBreakpointPasscount* passcountMessage = static_cast<comm::ScriptBreakpointPasscount*>( message.Get() );

		CScriptFile* file = FindScriptFile( passcountMessage->breakpoint.standard.file );

		if ( file )
		{
			script::RuntimeBreakpoint* breakpoint = file->FindBreakpoint( passcountMessage->breakpoint.position );

			if( breakpoint )
				breakpoint->SetPasscount( passcountMessage->passcount, passcountMessage->style );
		}
	}
	else if ( message->IsA( comm::EMessageID::ScriptBreakpointSetHitcount ) )
	{
		const comm::ScriptBreakpointSetHitcount* hitcountMessage = static_cast<comm::ScriptBreakpointSetHitcount*>( message.Get() );

		CScriptFile* file = FindScriptFile( hitcountMessage->breakpoint.standard.file );

		if ( file )
		{
			script::RuntimeBreakpoint* breakpoint = file->FindBreakpoint( hitcountMessage->breakpoint.position );

			if ( breakpoint )
				breakpoint->SetHitcount( hitcountMessage->hitcount );
		}
	}
	else if ( message->IsA( comm::EMessageID::ScriptResumeExecution ) )
	{
		m_breakAction = script::BreakAction::Breakpoint;
		m_breakForThreadId = 0;
	}
	else if ( message->IsA( comm::EMessageID::ScriptBreakRequest ) )
	{
		const comm::ScriptBreakRequest* breakMessage = static_cast<comm::ScriptBreakRequest*>( message.Get() );

		if ( breakMessage->type == "StepInto" )
		{
			m_breakAction = script::BreakAction::StepInto;
			m_breakForThreadId = breakMessage->breakpointThreadId;
		}
		else if ( breakMessage->type == "StepOver" )
		{
			m_breakAction = script::BreakAction::StepOver;
			m_breakForThreadId = breakMessage->breakpointThreadId;
		}
		else if ( breakMessage->type == "StepOut" )
		{
			m_breakAction = script::BreakAction::StepOut;
			m_breakForThreadId = breakMessage->breakpointThreadId;
		}
		else if ( breakMessage->type == "Pause" )
		{
			m_breakAction = script::BreakAction::Next;
			m_breakForThreadId = 0;
		}
	}

	return false;
}
