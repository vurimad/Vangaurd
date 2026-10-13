/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "profilerTypes.h"
#include "profilerConfiguration.h"
#include "profilerChannels.h"
#include "profilerManager.h"
#include "commandline.h"
#include "absolutePath.h"
//#include "configVar.h"

// tools
#include "profilerToolNvidia.h"
#include "profilerToolTracy.h"
#include "profilerToolPix.h"
#include "profilerToolRazor.h"
#include "profilerToolVTune.h"
#include "profilerToolRed.h"
#include "profilerToolRedInGame.h"

// extensions
#include "profilerExtMemory.h"

// system
#include "../../redSystem/include/redThreadsRedSystem.h"
#include "../../redMemory/include/uniquePtr.h"

//////////////////////////////////////////////////////////////////////////
// usings
using red::InstrumentationObject;
using red::RedProfilerTool;

#ifdef USE_RED_INGAME_PROFILER
using red::RedInGameProfilerTool;
#endif

RED_NO_EMPTY_FILE()


#ifdef USE_PROFILER

//////////////////////////////////////////////////////////////////////////
//
void REDCORE_API red::profiler::InitInGameProfiler()
{
#if defined(USE_PROFILER) && defined(USE_RED_INGAME_PROFILER)
	gRedInGameProfilerTool.Init( 32 * 1024 * 1024 );
#endif
}


//////////////////////////////////////////////////////////////////////////
// globals
REDCORE_API red::ProfilerManager gProfilers;


//////////////////////////////////////////////////////////////////////////
// profilers
RedProfilerTool			redProfilerTool;
RazorProfilerTool		razorProfilerTool;
PixProfilerTool			pixProfilerTool;
NvtxProfilerTool		nvProfilerTool;
TracyProfilerTool		tracyProfilerTool;
VTuneProfilerTool		vtuneProfilerTool;

//////////////////////////////////////////////////////////////////////////
// extensions
red::MemoryProfilerExtension memProfilerExtension;


namespace Config
{
/*
	TConfigVar<String> cvActiveProfilers( "Profiler", "ActiveProfilers", "none", eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar<Int32 > cvProfilingLevel( "Profiler", "ProfilingLevel", 4, 0, 4, eConsoleVarFlag_Developer );
	TConfigVar<String> cvProfilerServerName( "Profiler", "ProfilerServerName", "localhost", eConsoleVarFlag_Developer );*/
}

//#define PROFILE_THE_PROFILER - todo: will be used widely later

red::ProfilerManager::ProfilerManager() :
	m_init( false ),
	m_started( false ),
	m_changingState( false ),
	m_startedFromConsole( false ),
	m_isEditor( false ),
	m_activeHandlerCount( 0 ),
	m_catchBreakpointStarted( false )
{
	red::Memset( &m_handles[0], 0, sizeof(InstrumentationObject*)*PROFILER_MAX_SCOPES );
	red::Memset( &m_profilingToolsEnabled[0], 0, sizeof(Bool)*ProfilerToolSlot::PTS_MAX );

	m_startTicks = m_timer.GetTicks();
	m_stopTicks = m_startTicks;
}

red::ProfilerManager::~ProfilerManager()
{
	m_catchBreakpointStarted.SetValue( false );
    Stop();

	Uint32 count = m_activeHandlerCount.GetValue();
	for( Uint32 i=0; i<count; ++i )
	{
		//RED_DELETE( m_handles[i] );
	}
}
//////////////////////////////////////////////////////////////////////////
// return total profiler timer
Double red::ProfilerManager::GetProfilingTime()
{
	if ( m_started.GetValue() )  return (Double)(m_timer.GetTicks()-m_startTicks) / m_timer.GetFrequency();
	return (Double)(m_stopTicks-m_startTicks) / m_timer.GetFrequency();
}
//////////////////////////////////////////////////////////////////////////
// return registered functions count
Uint32 red::ProfilerManager::GetRegisteredFunctionsCount()
{
	return m_activeHandlerCount.GetValue();
}
//////////////////////////////////////////////////////////////////////////
// is profiler started
Bool red::ProfilerManager::IsStarted()
{
	return m_started.GetValue();
}
//////////////////////////////////////////////////////////////////////////
// is profiler initialized
Bool red::ProfilerManager::IsInitialized()
{
	return m_init.GetValue();
}
//////////////////////////////////////////////////////////////////////////
// return factor <0;1> of how internal buffer is filled
Float red::ProfilerManager::GetFilledBufferFactor( const ProfilerToolSlot profilerTool )
{
#ifdef USE_RED_PROFILER
	switch ( profilerTool )
	{
		case PTS_REDPROFILER: return redProfilerTool.GetFilledBufferFactor();

		default:
			return 1.0f;
	}
#endif

	// def result
	return 1.0f;
}
//////////////////////////////////////////////////////////////////////////
// init
Bool red::ProfilerManager::Init( Uint32 bufferSize )
{
	if ( m_changingState.CompareExchange( true, false ) == false )
	{
		//if ( mem == 0 )
		//	mem = Config::cvBufferSize.Get();

		// default memory value
		if( bufferSize == 0 )
		{
			bufferSize = PROFILER_DEFAULT_VMEM;
		}

		// temp removed - check absolute paths
		//m_instrFuncConfigLoaded.SetValue( LoadInstrFuncEnabledFile() );
		//DisableIntrFuncs();

		if ( m_catchBreakpointStarted.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Profiling can not be started during catch breakpoint!!!" );
			m_changingState.SetValue( false );
			return false;
		}

		/*if ( !m_init.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Profiling isn't initialized!!!" );
			m_changingState.SetValue( false );
			return;
		}*/

		if( m_started.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Profiling is running right now!!!" );
			m_changingState.SetValue( false );
			return false;
		}

		m_startTicks = m_timer.GetTicks();
		m_stopTicks = m_startTicks;

		// init tools
		#ifdef USE_RED_PROFILER
		redProfilerTool.Init( bufferSize );
		#endif

		#ifdef USE_RAZOR_PROFILER
		razorProfilerTool.Init( bufferSize );
		#endif

		#ifdef USE_PIX_PROFILER
		pixProfilerTool.Init( bufferSize );
		#endif

		#ifdef USE_VTUNE_PROFILER
		vtuneProfilerTool.Init( bufferSize );
		#endif

		#ifdef USE_NVIDIA_PROFILER
		nvProfilerTool.Init( bufferSize );
		#endif

		#ifdef USE_TRACY_PROFILER
		tracyProfilerTool.Init( bufferSize );
		#endif

		m_init.SetValue( true );
		m_started.SetValue( false );

		m_changingState.SetValue( false );

		m_startedFromConsole = red::CommandLine::Get().HasOption( "profilerAutorun" );
		if ( m_startedFromConsole )
		{
			Start();
		}

		return true;
	}
	else
	{
		RED_LOG_WARNING( "Profiler: Another changing profiler state command processing!!!" );
	}

	// def result
	return false;
}


//////////////////////////////////////////////////////////////////////////
// thread init
void red::ProfilerManager::InitThread( const AnsiChar *name, Uint32 maxNumSamples )
{
#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.InitThread( name, maxNumSamples );
#endif

#ifdef USE_VTUNE_PROFILER
	// Call even if not enabled, since thread can be created before and not a perf issue
	vtuneProfilerTool.InitThread( name );
#endif
}

//////////////////////////////////////////////////////////////////////////
// is profiler is running on editor side
void red::ProfilerManager::SetEditor( const Bool isEditor )
{
	m_isEditor = isEditor;
}

void red::ProfilerManager::Shutdown()
{
	if ( m_startedFromConsole )
	{
		Stop();
		Store( String::EMPTY() );
	}


	#ifdef USE_RED_PROFILER
	redProfilerTool.Shutdown();
	#endif

	#ifdef USE_RAZOR_PROFILER
	razorProfilerTool.Shutdown();
	#endif

	#ifdef USE_PIX_PROFILER
	pixProfilerTool.Shutdown();
	#endif

	#ifdef USE_VTUNE_PROFILER
	vtuneProfilerTool.Shutdown();
	#endif

	#ifdef USE_NVIDIA_PROFILER
	nvProfilerTool.Shutdown();
	#endif

	#ifdef USE_TRACY_PROFILER
	tracyProfilerTool.Shutdown();
	#endif
}

void red::ProfilerManager::Start()
{
	if ( !m_init.GetValue() )
	{
		Init( 0 ); // if RedProfiler.ini have BufferSize entry, initialization should be OK
	}

	if ( !m_started.GetValue() )
	{
		RED_LOG( "Profiler: started.." );

		m_started.SetValue( 1 );
	}
}

void red::ProfilerManager::Stop( const Bool skipChangingState )
{
	if( skipChangingState == true || m_changingState.CompareExchange( true, false ) == false )
	{
		if( m_started.GetValue() == false )
		{
			RED_LOG_WARNING( "Profiler: Profiling not started!!!" );
			m_changingState.SetValue( false );
			return;
		}

		const Uint64 time = m_timer.GetTicks();

		m_stopTicks = time;

		m_started.SetValue( false );

		// tools
		#ifdef USE_RED_PROFILER
		redProfilerTool.Stop();
		#endif

		#ifdef USE_RAZOR_PROFILER
		razorProfilerTool.Stop();
		#endif

		#ifdef USE_PIX_PROFILER
		pixProfilerTool.Stop();
		#endif

		#ifdef USE_NVIDIA_PROFILER
		nvProfilerTool.Stop();
		#endif

		#ifdef USE_TRACY_PROFILER
		tracyProfilerTool.Stop();
		#endif

		// extensions
		// dUd: todo

		RED_LOG( "Profiler: Stopped.." );

		// internal timer
		if( skipChangingState == false )
		{
			m_changingState.SetValue( false );
		}
	}
	else
	{
		RED_LOG_WARNING( "Profiler: Another changing profiler state command processing!!!" );
	}
}

Bool red::ProfilerManager::Store( const red::String& filename )
{
	if ( !m_init.GetValue() )
	{
		RED_LOG_WARNING( "Profiler: Profiling isn't initialized!!!" );
		return false;
	}

	if ( m_changingState.CompareExchange( true, false ) == false )
	{
		// stop profiler
		if ( m_started.GetValue() )
		{
			Stop( true );
		}

		// prepare output file path
		red::AbsolutePath output;
		red::profiler::PrepareOutputFilePath( filename, output );

		// tools
		#ifdef USE_RED_PROFILER
		redProfilerTool.Store( output.AsString(), m_handles, m_activeHandlerCount.GetValue(), m_isEditor );
		#endif

		m_changingState.SetValue( false );
	}
	else
	{
		RED_LOG_WARNING( "Profiler: Another changing profiler state command processing!!!" );
	}

    // def result
    return true;
}
//////////////////////////////////////////////////////////////////////////
// store to mem writer
Bool red::ProfilerManager::StoreToMem( red::DynArray<Uint8>& buffer )
{
	if ( !m_init.GetValue() )
	{
		RED_LOG_WARNING( "Profiler isn't initialized!!!" );
		return false;
	}

	if ( m_changingState.CompareExchange( true, false ) == false )
	{
		// stop profiler
		if ( m_started.GetValue() )
		{
			Stop( true );
		}

		RED_LOG( "Storing profile data to mem: [$%p]..", buffer.Data() );

		// tools
		#ifdef USE_RED_PROFILER
		redProfilerTool.StoreToMem( buffer, m_handles, m_activeHandlerCount.GetValue(), m_isEditor );
		#endif

		m_changingState.SetValue( false );
	}
	else
	{
		RED_LOG_WARNING( "Another changing profiler state command processing!!!" );
		return false;
	}

	// def result
	return true;
}

//////////////////////////////////////////////////////////////////////////
// preparing output file path
void red::profiler::PrepareOutputFilePath( const String& filename, red::AbsolutePath& outputPath )
{
	#if defined( RED_PLATFORM_DURANGO )
		outputPath = AbsolutePath::CreateDirPath( "d:\\" );
	#elif defined( RED_PLATFORM_ORBIS )
		outputPath = AbsolutePath::CreateDirPath( "/data/" );
	#else
		outputPath = AbsolutePath::CreateDirPath( PROFILER_OUTPUT_PATH );
	#endif

	// dUd: todo - create output directory
	//CSystemIO::CreateDirectory( m_outputPath.AsChar() );

	// init
	String outputFileName;

	// time stamp
	red::DateTime time;
	red::Clock::GetInstance().GetLocalTime( time );
	String date = String::Printf( "[%04d-%02d-%02d]_[%02d_%02d_%02d]",
		time.GetYear(), time.GetMonth() + 1, time.GetDay() + 1,
		time.GetHour(), time.GetMinute(), time.GetSecond() );

	// user name
	#if defined( RED_PLATFORM_WIN32 ) || defined ( RED_PLATFORM_WIN64 )
	char lpszUsername[255];
	DWORD dUsername = sizeof(lpszUsername);
	if ( GetUserNameA(lpszUsername, &dUsername ) )
	{
		outputFileName = lpszUsername;
	}
	#endif

	// prepare path
	if ( filename.Empty() )
	{
		outputFileName += "_default";
	}
	else
	{
		outputFileName += "_" + filename;
	}

	outputFileName += "_" + date;

	outputPath.AppendFilePath( outputFileName );
}

//////////////////////////////////////////////////////////////////////////
//
// fill instrFuncArray with register instrumented functions names
void red::ProfilerManager::GetInstrFuncArray( red::DynArray<InstrumentationObject*>& instrFuncArray )
{
	//const Uint32 instrFuncCount = m_activeHandlerCount.GetValue();
	//Uint32 instrFuncIndex = 0;
	//while ( instrFuncIndex < instrFuncCount)
	//{
	//	instrFuncArray.PushBack( *m_instrFuncs[instrFuncIndex] );
	//	instrFuncIndex++;
	//}
}
//////////////////////////////////////////////////////////////////////////
// register new instrumented object
InstrumentationObject* red::ProfilerManager::RegisterObject( InstrumentationObject* scope )
{
	const Uint32 oldCounter = m_activeHandlerCount.Increment()-1;
	if( oldCounter >= PROFILER_MAX_SCOPES )
	{
		m_activeHandlerCount.Decrement();
		RED_LOG_WARNING( "Profiler: Maximum number [%u] of InstrumentationObject registrations exceeded!!!", PROFILER_MAX_SCOPES );
		return nullptr;
	}

	scope->m_id = oldCounter;

#ifdef USE_VTUNE_PROFILER
	vtuneProfilerTool.InitBlock( scope, scope->m_name );
#endif

	// todo: disabling while adding a new one
	m_handles[ oldCounter ] = scope;
	scope->m_registered = true;

    return scope;
}
//////////////////////////////////////////////////////////////////////////
// disabled instrumented function
void red::ProfilerManager::EnableObject( InstrumentationObject* scope, Bool enable )
{
	scope->m_enabled.SetValue( enable );
}
//////////////////////////////////////////////////////////////////////////
// disabled instrumented function by ID
void red::ProfilerManager::EnableObject( Uint32 instrumentedFunctionId, Bool enable )
{
	if ( instrumentedFunctionId < PROFILER_MAX_SCOPES && m_handles[ instrumentedFunctionId ] )
	{
		m_handles[ instrumentedFunctionId ]->m_enabled.SetValue( enable );
	}
}

void red::ProfilerManager::PutSyncPoint( red::InstrumentationObject* block )
{
	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.PutSyncPoint( block );
	#endif
}

void red::ProfilerManager::BeginGroup( red::InstrumentationObject* block )
{
	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.BeginGroup( block );
	#endif
}

void red::ProfilerManager::EndGroup( red::InstrumentationObject* block )
{
	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.EndGroup( block );
	#endif
}

void red::ProfilerManager::StartBlock( InstrumentationObject* block, const char* scopeName )
{
	scopeName = (scopeName == nullptr) ? "" : scopeName;

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_RAZOR ] )
	{
		#ifdef USE_RAZOR_PROFILER
		razorProfilerTool.StartBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_PIX ] )
	{
		#ifdef USE_PIX_PROFILER
		pixProfilerTool.StartBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_VTUNE ] )
	{
		#ifdef USE_VTUNE_PROFILER
		vtuneProfilerTool.StartBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_NVIDIA ] )
	{
		#ifdef USE_NVIDIA_PROFILER
		nvProfilerTool.StartBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_TRACY ] )
	{
		#ifdef USE_TRACY_PROFILER
		tracyProfilerTool.StartBlock( block, scopeName );
		#endif
	}

	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.StartBlock( block, scopeName );
	#endif

	if ( !m_init.GetValue() || !m_started.GetValue() )
		return;

	//const Int32 profilerLevel = 4; //Config::cvProfilingLevel.Get();
	//const Int32 profilerActiveChannels = 0xFFFFFFFF; //Config::cvProfilingChannels.Get();

	//block->m_canExecute =	(block->m_handle != nullptr) &&								// there is a handle
	//						(block->m_handle->m_level <= profilerLevel) &&				// handle level is less or equals to current profiling level
	//						(block->m_handle->m_channels & profilerActiveChannels );	// there is a channel in that handle which we listen to

	//if( block->m_canExecute == false )
	{
	//	return;
	}

	// todo:
	// breakpoints

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_REDPROFILER ] )
	{
		#ifdef USE_RED_PROFILER
		redProfilerTool.StartBlock( block, scopeName );
		#endif
	}
}

void red::ProfilerManager::StopBlock( InstrumentationObject* block, const char* scopeName )
{
	scopeName = (scopeName == nullptr) ? "" : scopeName;

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_RAZOR ] )
	{
		#ifdef USE_RAZOR_PROFILER
		razorProfilerTool.StopBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_PIX ] )
	{
		#ifdef USE_PIX_PROFILER
		pixProfilerTool.StopBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_VTUNE ] )
	{
		#ifdef USE_VTUNE_PROFILER
		vtuneProfilerTool.StopBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_NVIDIA ] )
	{
		#ifdef USE_NVIDIA_PROFILER
		nvProfilerTool.StopBlock( block, scopeName );
		#endif
	}

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_TRACY ] )
	{
		#ifdef USE_TRACY_PROFILER
		tracyProfilerTool.StopBlock( block, scopeName );
		#endif
	}

	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.StopBlock( block, scopeName );
	#endif

	if ( !m_init.GetValue() || !m_started.GetValue() )
		return;

	//if( block->m_canExecute == false )
	{
	//	return;
	}

	// todo:
	// breakpoints

	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_REDPROFILER ] )
	{
		#ifdef USE_RED_PROFILER
		redProfilerTool.StopBlock( block, scopeName );
		#endif
	}
}

void red::ProfilerManager::Signal( InstrumentationObject* block )
{
	// todo:
}

void red::ProfilerManager::Signal( InstrumentationObject* block, const char* msg )
{
	if ( !m_init.GetValue() || !m_started.GetValue() )
		return;

	//const Int32 profilingLevel = 4; //Config::cvProfilingLevel.Get();
	//const Int32 profilerActiveChannels = 0xFFFFFFFF; //Config::cvProfilingChannels.Get();

	//block->m_canExecute =	(block->m_handle != nullptr) &&								// there is a handle
	//						(block->m_handle->m_level <= profilingLevel) &&				// handle level is less or equals to current profiling level
	//						(block->m_handle->m_channels & profilerActiveChannels );	// there is a channel in that handle which we listen to

	//if( block->m_canExecute == false )
	{
	//	return;
	}

	// todo:
}

Bool red::ProfilerManager::InitExtension( red::ProfilerExtensionSlot ext, Uint32 bufferSize )
{
	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		return memProfilerExtension.Init( bufferSize );
	}
	return false;
}

void red::ProfilerManager::StartExtension( ProfilerExtensionSlot ext, red::memory::OutOfProfilerMemoryCallback outOfProfilerMemoryCallback )
{
	struct OOMProfilerHandler
	{
		OOMProfilerHandler( red::memory::OutOfProfilerMemoryCallback&& outOfProfilerMemoryCallback )
			: outOfProfilerMemoryCallback( red::CreateUniquePtr< red::memory::OutOfProfilerMemoryCallback, red::PoolDebug >( std::move( outOfProfilerMemoryCallback ) ) )
		{}

		void operator()()
		{
			( *outOfProfilerMemoryCallback )();
			PROFILER_ExtensionOOM( ProfilerExtensionSlot::PES_MEMORY );
		}

		red::UniquePtr< red::memory::OutOfProfilerMemoryCallback, red::PoolDebug > outOfProfilerMemoryCallback;
	};

	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		memProfilerExtension.Start( OOMProfilerHandler( std::move( outOfProfilerMemoryCallback ) ) );
	}
}

void red::ProfilerManager::StopExtension( ProfilerExtensionSlot ext )
{
	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		memProfilerExtension.Stop();
	}
}

void red::ProfilerManager::StoreExtension( red::ProfilerExtensionSlot ext, const red::String& filename )
{
	// prepare output file path
	red::AbsolutePath output;
	red::profiler::PrepareOutputFilePath( filename, output );

	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		memProfilerExtension.Store( output.AsString() );
	}
}

void red::ProfilerManager::StoreExtension( red::ProfilerExtensionSlot ext, const red::String& filename, red::AbsolutePath& filePath )
{
	// prepare output file path
	red::AbsolutePath output;
	red::profiler::PrepareOutputFilePath( filename, output );

	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		memProfilerExtension.Store( output.AsString() );
		filePath = output;
		filePath.AppendFilePath( ".rmm" );
	}
}

void red::ProfilerManager::ExtensionOOM( red::ProfilerExtensionSlot ext )
{
	if ( m_profilingExtensionsEnabled[ ext ] )
	{
		memProfilerExtension.SetOutOfMemory();
	}
}

red::memory::Stream* red::ProfilerManager::GetExtensionStream( red::ProfilerExtensionSlot ext )
{
	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		return &memProfilerExtension.GetOutputStream();
	}

	// null stream
	return nullptr;
}

Float red::ProfilerManager::GetExtensionFilledBufferFactor( red::ProfilerExtensionSlot ext )
{
	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		return memProfilerExtension.GetFilledBufferFactor();
	}

	return 0.f;
}

void red::ProfilerManager::NextFrame( ProfilerFrameType frameType )
{
	#ifdef USE_RAZOR_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_RAZOR ] )
	{
		razorProfilerTool.NextFrame( frameType );
	}
	#endif

	#ifdef USE_PIX_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_PIX ] )
	{
		pixProfilerTool.NextFrame( frameType );
	}
	#endif

	#ifdef USE_VTUNE_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_VTUNE ] )
	{
		vtuneProfilerTool.NextFrame( frameType );
	}
	#endif

	#ifdef USE_NVIDIA_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_NVIDIA ] )
	{
		nvProfilerTool.NextFrame( frameType );
	}
	#endif

	#ifdef USE_TRACY_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_TRACY ] )
	{
		tracyProfilerTool.NextFrame( frameType );
	}
	#endif

	#ifdef USE_RED_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_REDPROFILER ] )
	{
		if ( m_init.GetValue() && m_started.GetValue() )
		{
			redProfilerTool.NextFrame( frameType );
		}
	}
	#endif

	#ifdef USE_PROFILER
	if ( m_profilingExtensionsEnabled[ ProfilerExtensionSlot::PES_MEMORY ] )
	{
		if ( memProfilerExtension.IsOutOfMemory() )
		{
			memProfilerExtension.Stop();
		}
		else if( memProfilerExtension.IsRunning() )
		{
			memProfilerExtension.NextFrame( frameType );
		}
	}
	#endif

	#ifdef USE_RED_INGAME_PROFILER
	gRedInGameProfilerTool.NextFrame( frameType );
	#endif
}
//////////////////////////////////////////////////////////////////////////
// update tools and extensions
void red::ProfilerManager::Update()
{
	#ifdef USE_RAZOR_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_RAZOR ] )
	{
		razorProfilerTool.Update();
	}
	#endif

	#ifdef USE_PIX_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_PIX ] )
	{
		pixProfilerTool.Update();
	}
	#endif

	#ifdef USE_NVIDIA_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_NVIDIA ] )
	{
		nvProfilerTool.Update();
	}
	#endif

	#ifdef USE_TRACY_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_TRACY ] )
	{
		tracyProfilerTool.Update();
	}
	#endif

	#ifdef USE_RED_PROFILER
	if ( m_profilingToolsEnabled[ ProfilerToolSlot::PTS_REDPROFILER ] )
	{
		if ( m_init.GetValue() && m_started.GetValue() )
		{
			redProfilerTool.Update();
		}
	}
	#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// start catch profiler breakpoints
void red::ProfilerManager::StartCatchBreakpoint()
{
	if ( m_changingState.CompareExchange( true, false ) == false )
	{
		if ( m_started.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Catch breakpoint can not be started during profile session!!!" );
			m_changingState.SetValue( false );
			return;
		}
		if ( m_catchBreakpointStarted.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Catch breakpoint already started!!!" );
			m_changingState.SetValue( false );
			return;
		}
		m_catchBreakpointStarted.SetValue( true );
		m_changingState.SetValue( false );
	}
	else
	{
		RED_LOG_WARNING( "Profiler: Another changing profiler state command processing!!!" );
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// stop catch profiler breakpoint
void red::ProfilerManager::StopCatchBreakpoint()
{
	if ( m_changingState.CompareExchange( true, false ) == false )
	{
		if ( !m_catchBreakpointStarted.GetValue() )
		{
			RED_LOG_WARNING( "Profiler: Catch breakpoint not started!!!" );
			m_changingState.SetValue( false );
			return;
		}
		m_catchBreakpointStarted.SetValue( false );
		m_changingState.SetValue( false );
	}
	else
	{
		RED_LOG_WARNING( "Profiler: Another changing profiler state command processing!!!" );
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// set time base profiler breakpoint
// timeInMs - time in MS, if execute time is longer then timeInMs breakpoint is hit
// stopOnce - if true breakpoint is hit only once
void red::ProfilerManager::SetTimeBreakpoint( const char* name, Float timeInMs, Bool stopOnce )
{
	if ( m_catchBreakpointStarted.GetValue() )
	{
		RED_LOG_WARNING( "Profiler: Before set time breakpoint stop catching breakpoint proccess!!!" );
		m_changingState.SetValue( false );
		return;
	}
	const Uint32 instrFuncCount = m_activeHandlerCount.GetValue();
	Uint32 instrFuncIndex = 0;
	while ( instrFuncIndex < instrFuncCount)
	{
		if ( strcmp( m_handles[instrFuncIndex]->m_name, name ) == 0 )
		{
			m_handles[instrFuncIndex]->m_breakpointAtExecTime = timeInMs;
			m_handles[instrFuncIndex]->m_breakOnce = stopOnce;
			m_handles[instrFuncIndex]->m_onceStopped.SetValue( false );
			return;
		}
		++instrFuncIndex;
	}
	RED_LOG_WARNING( "Profiler: %hs is not registered!!!", name );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// set hit count profiler breakpoint
// hitCount - count of hits
void red::ProfilerManager::SetHitCountBreakpoint( const char* name, Uint32 hitCount )
{
	if ( m_catchBreakpointStarted.GetValue() )
	{
		RED_LOG_WARNING( "Profiler: Before set hit count breakpoint stop catching breakpoint proccess!!!" );
		m_changingState.SetValue( false );
		return;
	}
	const Uint32 instrFuncCount = m_activeHandlerCount.GetValue();
	Uint32 instrFuncIndex = 0;
	while ( instrFuncIndex < instrFuncCount )
	{
		if ( strcmp( m_handles[instrFuncIndex]->m_name, name ) == 0 )
		{
			m_handles[instrFuncIndex]->m_breakpointAtHitCount.SetValue( hitCount );
			return;
		}
		++instrFuncIndex;
	}
	RED_LOG_WARNING( "Profiler: %hs is not registered!!!", name );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
// remove time base profiler breakpoint
void red::ProfilerManager::DisableTimeBreakpoint( const char* name )
{
	if ( m_catchBreakpointStarted.GetValue() )
	{
		RED_LOG_WARNING( "Profiler: Before disable time breakpoint stop catching breakpoint proccess!!!" );
		m_changingState.SetValue( false );
		return;
	}
	SetTimeBreakpoint( name, 0.0f, false );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
//
//remove hit count profiler breakpoint
void red::ProfilerManager::DisableHitCountBreakpoint( const char* name )
{
	if ( m_catchBreakpointStarted.GetValue() )
	{
		RED_LOG_WARNING( "Profiler: Before disable hit count breakpoint stop catching breakpoint proccess!!!" );
		m_changingState.SetValue( false );
		return;
	}
	SetHitCountBreakpoint( name, 0 );
}

#endif