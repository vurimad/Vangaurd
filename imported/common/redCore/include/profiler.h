/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

/* todo:
	How to use this thread-safe lightweight profiling system.

	1. Remember that although lightweight, profiler is not cost-free. Try to profile one thing at once not all.
	3. PerfCounters can be nested providing a stack trace profiling
	4. PerfCounters can have their own grouping for better visual feedback.

	Profile block looks like this

	{
		PC_SCOPE( MyBlock )
		... code ...
	}

	In order to count frame ticks somewhere in code there should be a line PC_TICK()
	Dynamic profilers are currently not supported but this is an option. They are not supported because
	name is a static literal and name comparisons are based on pointers (means very fast). Dynamic names are a little more tedious though.

	For all you guys and gels that would like to "enhance" profiling system. This is a bare minimum to profile thus lightweight. Adding any other "smart" logic
	will downgrade performance and could potentially provide non-thread-safe system (deadlocks etc.)

	Any "smart" logic is preferably contained in wrapper classes like CProfilerStatBox, that gathers samples, counts average of last STAT_SIZE samples.
	Any hi-level logic is done elsewhere (on debugPages) so when off id doesn't add overhead. Consult profile debug pages to see how this works.

*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "profilerConfiguration.h"
#include "profilerChannels.h"
#include "profilerManager.h"
#include "../../redSystem/include/redThreadsThread.h"


// profiler
#ifdef USE_PROFILER

	// FIX: add ##name to __handle_, but this means that PC_SCOPEs need some fixes
	//
	#define PC_SCOPE_INST_OBJ( instObj, name )																								\
		if ( !(instObj).m_registered )																										\
			gProfilers.RegisterObject( &(instObj) );																						\
		red::InstrumentationScope RED_UNIQUE_NAME( scope )( (instObj), name );

	#define PC_SCOPE_VAR( var, name )																										\
		static red::InstrumentationObject RED_CONCATENATE( __instrObj_, var )( #name );														\
		PC_SCOPE_INST_OBJ( RED_CONCATENATE( __instrObj_, var ), #name )

	#define PC_SCOPE_VAR_NAMED( var, str )																									\
		static red::InstrumentationObject RED_CONCATENATE( __instrObj_, var )( str );														\
		PC_SCOPE_INST_OBJ( RED_CONCATENATE( __instrObj_, var ), str )

	#define PC_SCOPE( name )					PC_SCOPE_VAR( __LINE__, name )
	#define PC_SCOPE_FUNC()						PC_SCOPE_VAR_NAMED( __LINE__, RED_FUNCTION )


	#define PC_SYNC_POINT_INST_OBJ( instObj )																								\
		if( !(instObj).m_registered )																										\
			gProfilers.RegisterObject( &(instObj) );																						\
		gProfilers.PutSyncPoint( &(instObj) );

	#define PC_SYNC_POINT( name )																											\
		static red::InstrumentationObject RED_CONCATENATE( __instrObj_, __LINE__ )( #name );												\
		PC_SYNC_POINT_INST_OBJ( RED_CONCATENATE( __instrObj_, __LINE__ ) )																	\

	#define PC_BEGIN_GROUP( instObj )																										\
		if( !(instObj).m_registered )																										\
				gProfilers.RegisterObject( &(instObj) );																					\
			gProfilers.BeginGroup( &(instObj) );

	#define PC_END_GROUP( instObj )																											\
		if( !(instObj).m_registered )																										\
				gProfilers.RegisterObject( &(instObj) );																					\
			gProfilers.EndGroup( &(instObj) );

	//#define PC_SIGNAL( handler, value )		UnifiedProfilerManager::GetInstance().Message( handler, value );
	//#define PC_SIGNAL_WITH_NAME( name, value, level, channel )  static CProfilerHandle* __handle_##name = UnifiedProfilerManager::GetInstance().RegisterHandler( #name, level, channel );	\
	//											CProfilerBlock __profilerBlock_##name( __handle_##name );	\
	//											PC_SIGNAL( &__profilerBlock_##name, value );

	// NOT making the instrumentation object static local variable to avoid the extra cost of checking if the initialization happened 
	// (especially, since, c++11 it needs to ensure that it's done in thread-safe manner)
	#define DECLARE_JOB_PROFILER_INTERFACE()																								\
		public:																																\
			virtual red::InstrumentationObject* GetInstrumentationObject() const { return &sm_instrumentationObject; }						\
		private:																															\
			static red::InstrumentationObject	sm_instrumentationObject;

	#define DEFINE_JOB_PROFILER_INTERFACE( className )				red::InstrumentationObject									className::sm_instrumentationObject( #className )
	#define DEFINE_JOB_PROFILER_INTERFACE_TEMPLATE( className )		template< class _T > red::InstrumentationObject				className<_T>::sm_instrumentationObject( #className )
	#define DEFINE_JOB_PROFILER_INTERFACE_TEMPLATE2( className )	template< class _T0, class _T1 > red::InstrumentationObject	className< _T0, _T1 >::sm_instrumentationObject( #className )
#else

	#define PC_SCOPE_INST_OBJ( instObj, name )
	#define PC_SCOPE(name)
	#define PC_SCOPE_FUNC()
	#define PC_SCOPE_VAR_NAMED( var, str )

	#define PC_SYNC_POINT_INST_OBJ( instObj )
	#define PC_SYNC_POINT( name )
	#define PC_BEGIN_GROUP( instObj )
	#define PC_END_GROUP( instObj )

	//#define PC_SIGNAL( handler, value )
	//#define PC_SIGNAL_WITH_NAME( name, value, level, channel )

	#define DECLARE_JOB_PROFILER_INTERFACE()
	#define DEFINE_JOB_PROFILER_INTERFACE( className )
	#define DEFINE_JOB_PROFILER_INTERFACE_TEMPLATE( className )	
	#define DEFINE_JOB_PROFILER_INTERFACE_TEMPLATE2( className )	

#endif

namespace red
{
	extern REDCORE_API EProfilerBlockChannel SwapThreadLocalPerfChannels( EProfilerBlockChannel channels);
}

struct ScopedProfilerChannel
{
	explicit ScopedProfilerChannel( EProfilerBlockChannel channel )
	{
		m_prevChannel = red::SwapThreadLocalPerfChannels( channel );
	}

	~ScopedProfilerChannel()
	{
		red::SwapThreadLocalPerfChannels( m_prevChannel );
	}

	ScopedProfilerChannel( const ScopedProfilerChannel& ) = delete;
	ScopedProfilerChannel( ScopedProfilerChannel&& ) = delete;

	EProfilerBlockChannel m_prevChannel;
};