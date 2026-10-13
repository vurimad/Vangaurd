/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "../../redContainers/include/dynArray.h"
#include "../../redContainers/include/string/string.h"
#include "../../redMemory/include/memoryStream.h"
#include "../../redMemory/include/metricsUtils.h"
#include "profilerConfiguration.h"
#include "profilerTypes.h"
#include "instrumentationObject.h"

const Uint32 PROFILER_MAX_SCOPES = 65536;

#ifdef USE_PROFILER

namespace red
{
	class AbsolutePath;

	extern REDCORE_API EProfilerBlockChannel SwapThreadLocalPerfChannels( EProfilerBlockChannel channels);

	// helpers
	namespace profiler
	{
		void PrepareOutputFilePath( const String& filename, red::AbsolutePath& outputPath );
	}

	namespace profiler
	{
		void REDCORE_API InitInGameProfiler();
	}

	// declarations
	class REDCORE_API ProfilerManager
	{
	public:
		ProfilerManager();
		~ProfilerManager();

		// common
		void SetEditor( const Bool isEditor );
		Bool Init( Uint32 bufferSize );
		void Shutdown();
		void Update();

		void InitThread( const AnsiChar *name, Uint32 maxNumSamples );

		void Start();
		void Stop( const Bool skipChangingState = false );
		Bool Store( const red::String& filename );
		Bool StoreToMem( red::DynArray<Uint8>& buffer );

		// accessors
		Bool IsStarted();
		Bool IsInitialized();

		Float GetFilledBufferFactor( const ProfilerToolSlot profilerTool );

		Double GetProfilingTime();
		Uint32 GetRegisteredFunctionsCount();

		// breakpoints
		void StartCatchBreakpoint();
		void StopCatchBreakpoint();
		void SetTimeBreakpoint( const char* name, Float timeInMs, Bool stopOnce );
		void SetHitCountBreakpoint( const char* name, Uint32 hitCount );
		void DisableTimeBreakpoint( const char* name );
		void DisableHitCountBreakpoint( const char* name );

		// instrumented scopes
		red::InstrumentationObject* RegisterObject( red::InstrumentationObject* obj );
		void EnableObject( red::InstrumentationObject* obj, Bool enable );
		void EnableObject( Uint32 scopeId, Bool enable );

		// profiling tools
		void PutSyncPoint( red::InstrumentationObject* block );
		void BeginGroup( red::InstrumentationObject* block );
		void EndGroup( red::InstrumentationObject* block );

		void StartBlock( red::InstrumentationObject* block, const char* scopeName = nullptr );		//const;	FIX const after profiling profiler is done
		void StopBlock( red::InstrumentationObject* block, const char* scopeName = nullptr );		//const;
		void Signal( red::InstrumentationObject* block );
		void Signal( red::InstrumentationObject* block, const char* msg );

		// fill instrFuncArray with register instrumented functions names
		void GetInstrFuncArray( red::DynArray<red::InstrumentationObject*>& instrFuncArray );

		// tools
		RED_INLINE void EnableTool( red::ProfilerToolSlot tool, Bool enable )
		{
			m_profilingToolsEnabled[ tool ] = enable;
		}

		RED_INLINE Bool IsToolEnabled( red::ProfilerToolSlot tool )
		{
			return  m_profilingToolsEnabled[ tool ];
		}

		// extensions
		RED_INLINE void EnableExtension( red::ProfilerExtensionSlot ext, Bool enable )
		{
			m_profilingExtensionsEnabled[ext] = enable;
		}

		RED_INLINE Bool IsExtensionEnabled( red::ProfilerExtensionSlot ext )
		{
			return m_profilingExtensionsEnabled[ext];
		}

		// end frame markers
		void NextFrame( red::ProfilerFrameType frameType );

		// profiling extensions
		Bool InitExtension( red::ProfilerExtensionSlot ext, Uint32 bufferSize );
		void StartExtension( red::ProfilerExtensionSlot ext, red::memory::OutOfProfilerMemoryCallback outOfProfilerMemoryCallback = []{} );
		void StopExtension( red::ProfilerExtensionSlot ext );
		void StoreExtension( red::ProfilerExtensionSlot ext, const red::String& filename );
		void StoreExtension( red::ProfilerExtensionSlot ext, const red::String& filename, red::AbsolutePath& filePath );
		void ExtensionOOM( red::ProfilerExtensionSlot ext );
		red::memory::Stream* GetExtensionStream( red::ProfilerExtensionSlot ext );
		Float GetExtensionFilledBufferFactor( red::ProfilerExtensionSlot ext );

		 //! overhead info variables
		red::Atomic<Bool>	m_init;
		red::Atomic<Bool>	m_started;
		red::Atomic<Bool>	m_changingState;
		Uint64				m_startTicks;
		Uint64				m_stopTicks;
		red::Timer			m_timer;
		Bool				m_startedFromConsole;
		Bool				m_isEditor;

		red::InstrumentationObject*				m_handles[ PROFILER_MAX_SCOPES ];
		red::Atomic< Uint32 >					m_activeHandlerCount;
		//red::HashMap<red::String, Bool>		m_instrFuncDisabled;
		red::Atomic<Bool>						m_catchBreakpointStarted;

	private:
		Bool				m_profilingToolsEnabled[ red::ProfilerToolSlot::PTS_MAX ];
		Bool				m_profilingExtensionsEnabled[ red::ProfilerExtensionSlot::PES_MAX ];
	};
}

extern REDCORE_API red::ProfilerManager gProfilers;

namespace red
{
	struct InstrumentationScope
	{
		RED_INLINE explicit InstrumentationScope( red::InstrumentationObject& block, const char* scopeName )
			: m_block( block )
			, m_scopeName( scopeName )
			, m_prevChannel( PBC_NONE )
		{
			if( m_block.m_forceChannel != PBC_NONE )
			{
				m_prevChannel = red::SwapThreadLocalPerfChannels( block.m_forceChannel );
			}

			gProfilers.StartBlock( &block, scopeName );
		}

		RED_INLINE ~InstrumentationScope()
		{
			gProfilers.StopBlock( &m_block, m_scopeName );

			if( m_block.m_forceChannel != PBC_NONE )
			{
				red::SwapThreadLocalPerfChannels( m_prevChannel );
			}
		}

	private:
		red::InstrumentationObject& m_block;
		const char* m_scopeName;
		EProfilerBlockChannel m_prevChannel;
	};
}

#endif

//////////////////////////////////////////////////////////////////////////
// macros
#define PROFILER_DEFAULT_MEM		1024*1024*10
#define PROFILER_DEFAULT_VMEM		1024*1024*1024
#define PROFILER_MIN_MEM			128
#define PROFILER_OUTPUT_PATH		"S:\\"


#ifdef USE_PROFILER
	//#define PROFILER_DEFINE( INSTRFUNC ) red::InstrumentedScope __instrFunc__##INSTRFUNC; gProfilers.RegisterInstrFunc( __instrFunc__##INSTRFUNC )
	//#define PROFILER_DECLARE( INSTRFUNC ) red::InstrumentedScope __instrFunc__##INSTRFUNC
	//#define PROFILER_DECLARE_Extern( INSTRFUNC ) extern red::InstrumentedScope* __instrFunc__##INSTRFUNC

	//#define PROFILER_HANDLE_NAME( INSTRFUNC ) ptrHandle##INSTRFUNC

	#define PROFILER_SetEditor( IsEditor ) gProfilers.SetEditor( IsEditor );
	#define PROFILER_Init( bufferSize ) gProfilers.Init( bufferSize )
	#define PROFILER_InitEx( bufferSize, MEMORY_SIGNALS ) gProfilers.Init( bufferSize )
	#define PROFILER_InitThread( name, maxSamples ) gProfilers.InitThread( name, maxSamples )
	#define PROFILER_IsRecording() gProfilers.IsStarted()
	#define PROFILER_IsInitialized() gProfilers.IsInitialized()
	#define PROFILER_Update() gProfilers.Update()
	#define PROFILER_Start() gProfilers.Start()
	#define PROFILER_Stop() gProfilers.Stop()
	#define PROFILER_Store( path ) gProfilers.Store( path )
	#define PROFILER_StoreToMem( MEM ) gProfilers.StoreToMem( MEM )
	#define PROFILER_GetBufferUsage( a ) gProfilers.GetFilledBufferFactor( a )
	#define PROFILER_StoreInstrFuncList() gProfilers.StoreInstrFuncList()

	// profiling tools
	#define PROFILER_EnableTool( profilerTool, enable ) gProfilers.EnableTool( profilerTool, enable )
	#define PROFILER_IsToolEnabled( profilerTool ) gProfilers.IsToolEnabled( profilerTool )
	#define PROFILER_NextFrame( frameType ) gProfilers.NextFrame( frameType )

	#define PROFILER_StartBlock( INSTRFUNC ) gProfilers.StartBlock( __instrFunc__##INSTRFUNC )
	#define PROFILER_EndBlock( INSTRFUNC ) gProfilers.EndBlock( __instrFunc__##INSTRFUNC, 0 )

	#define PROFILER_StartBlockWithBreak( INSTRFUNC ) gProfilers.StartBlock( __instrFunc__##INSTRFUNC )
	#define PROFILER_EndBlockWithBreak( INSTRFUNC, START_TIME ) gProfilers.EndBlock( __instrFunc__##INSTRFUNC, START_TIME )

	#define PROFILER_Signal( INSTRFUNC ) gProfilers.Signal( __instrFunc__##INSTRFUNC )
	#define PROFILER_SignalValue( INSTRFUNC, VALUE ) gProfilers.Signal( __instrFunc__##INSTRFUNC, VALUE )

	#define PROFILER_StartCatchBreakpoint() gProfilers.StartCatchBreakpoint()
	#define PROFILER_StopCatchBreakpoint() gProfilers.StopCatchBreakpoint()
	#define PROFILER_SetTimeBreakpoint( NAME, TIME, STOP_ONCE ) gProfilers.SetTimeBreakpoint( NAME, TIME, STOP_ONCE )
	#define PROFILER_SetHitCountBreakpoint( NAME, COUNTER ) gProfilers.SetHitCountBreakpoint( NAME, COUNTER )
	#define PROFILER_DisableTimeBreakpoint( NAME ) gProfilers.DisableTimeBreakpoint( NAME )
	#define PROFILER_DisableHitCountBreakpoint( NAME ) gProfilers.DisableHitCountBreakpoint( NAME )

	// profiling extensions
	#define PROFILER_INTERNAL_StartExtension_1( profilerExtension ) gProfilers.StartExtension( profilerExtension )
	#define PROFILER_INTERNAL_StartExtension_2( profilerExtension, profilerStoppedCallback ) gProfilers.StartExtension( profilerExtension, profilerStoppedCallback )

	#define PROFILER_EnableExtension( profilerExtension, enable ) gProfilers.EnableExtension( profilerExtension, enable )
	#define PROFILER_IsExtensionEnabled( profilerExtension ) gProfilers.IsExtensionEnabled( profilerExtension )

	#define PROFILER_InitExtension( profilerExtension, bufferSize ) gProfilers.InitExtension( profilerExtension, bufferSize )
	#define PROFILER_StartExtension( ... ) RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( PROFILER_INTERNAL_StartExtension_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define PROFILER_StopExtension( profilerExtension ) gProfilers.StopExtension( profilerExtension )
	#define PROFILER_StoreExtension( profilerExtension, path ) gProfilers.StoreExtension( profilerExtension, path )
	#define PROFILER_StoreExtensionAndGetFilePath( profilerExtension, path, fullPath ) gProfilers.StoreExtension( profilerExtension, path, fullPath )
	#define PROFILER_GetExtensionStream( profilerExtension ) gProfilers.GetExtensionStream( profilerExtension )
	#define PROFILER_GetExtensionBufferUsage( profilerExtension ) gProfilers.GetExtensionFilledBufferFactor( profilerExtension )
	#define PROFILER_ExtensionOOM( profilerExtension ) gProfilers.ExtensionOOM( profilerExtension )
#else

	//#define PROFILER_DEFINE( INSTRFUNC )
	//#define PROFILER_DECLARE( INSTRFUNC )
	//#define PROFILER_DECLARE_Extern( INSTRFUNC )

	//#define PROFILER_HANDLE_NAME( INSTRFUNC )

	#define PROFILER_SetEditor( IsEditor )
	#define PROFILER_Init( MEMORY ) false
	#define PROFILER_InitEx( MEMORY, MEMORY_SIGNALS )
	#define PROFILER_InitThread( name, maxSamples )
	#define PROFILER_Start()
	#define PROFILER_IsRecording() false
	#define PROFILER_IsInitialized() false
	#define PROFILER_Update()
	#define PROFILER_Stop()
	#define PROFILER_Store( PATH ) false
	#define PROFILER_StoreToMem( MEM ) false
	#define PROFILER_GetBufferUsage( a ) 1.0f
	#define PROFILER_StoreInstrFuncList()

	// profiling tools
	#define PROFILER_EnableTool( profilerTool, enable )
	#define PROFILER_IsToolEnabled( profilerTool )
	#define PROFILER_NextFrame( frameType )

	#define PROFILER_StartBlock( INSTRFUNC )
	#define PROFILER_EndBlock( INSTRFUNC )

	#define PROFILER_StartBlockWithBreak( INSTRFUNC ) 0
	#define PROFILER_EndBlockWithBreak( INSTRFUNC, START_TIME )

	#define PROFILER_Signal( INSTRFUNC )
	#define PROFILER_SignalValue( INSTRFUNC, VALUE )

	#define PROFILER_StartCatchBreakpoint()
	#define PROFILER_StopCatchBreakpoint()
	#define PROFILER_SetTimeBreakpoint( NAME, TIME, STOP_ONCE )
	#define PROFILER_SetHitCountBreakpoint( NAME, COUNTER )
	#define PROFILER_DisableTimeBreakpoint( NAME )
	#define PROFILER_DisableHitCountBreakpoint( NAME )

	// profiling tools
	#define PROFILER_EnableExtension( profilerExtension, enable )
	#define PROFILER_IsExtensionEnabled( profilerExtension )

	#define PROFILER_InitExtension( profilerExtension, bufferSize ) false
	#define PROFILER_StartExtension( ... )
	#define PROFILER_StopExtension( profilerExtension )
	#define PROFILER_StoreExtension( profilerExtension, path )
	#define PROFILER_StoreExtensionAndGetFilePath( profilerExtension, path, fullPath )
	#define PROFILER_GetExtensionStream( profilerExtension ) nullptr

#endif //NEW_PROFILER_ENABLED
