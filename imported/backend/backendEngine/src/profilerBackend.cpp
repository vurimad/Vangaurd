/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../../common/engine/include/baseEngine.h"
#include "../../../common/engine/src/profilerControlCenter.h"
#include "profilerBackend.h"


red::ProfilerBackend::ProfilerBackend()
{
}

red::ProfilerBackend::~ProfilerBackend()
{
}

void red::ProfilerBackend::Update()
{
}

void red::ProfilerBackend::SetupInterop( ProfilerBackend* ptr, ::interop::utils::Object& op )
{
	// init
	op.RegisterFunctor<Bool, Uint32>( "init", []( interop::IContext& ctx, const Uint32 mem )
	{
		// min memory check
		if ( mem < PROFILER_MIN_MEM )
		{
			return false;
		}

		return PROFILER_Init( mem );
	} );

	// red profiler
	// start
	op.RegisterFunctor<void>( "start", []( interop::IContext& ctx )
	{
		PROFILER_Start();
	} );

	// stop
	op.RegisterFunctor<void>( "stop", []( interop::IContext& ctx )
	{
		PROFILER_Stop();
	} );

	// store
	op.RegisterFunctor<void, String>( "store", []( interop::IContext& ctx, const String& path )
	{
		PROFILER_Store( path );
	} );

	// extensions
	op.RegisterFunctor<void, Uint32>( "startext", []( interop::IContext& ctx, const Uint32 index )
	{
		if ( index < ProfilerExtensionSlot::PES_MAX )
		{
			red::ProfilerExtensionSlot extension = static_cast<red::ProfilerExtensionSlot>( index );
			PROFILER_InitExtension( extension, PROFILER_DEFAULT_VMEM );
			PROFILER_StartExtension( extension );
			return;
		}
		ctx.ThrowException( "index of out bound!" );
	} );

	op.RegisterFunctor<void, Uint32, String>( "stopext", []( interop::IContext& ctx, const Uint32 index, const String& path )
	{
		if ( index < ProfilerExtensionSlot::PES_MAX )
		{
			red::ProfilerExtensionSlot extension = static_cast<red::ProfilerExtensionSlot>( index );
			PROFILER_StopExtension( extension );
			PROFILER_StoreExtension( extension, path );
			return;
		}
		ctx.ThrowException( "index of out bound!" );
	} );
}