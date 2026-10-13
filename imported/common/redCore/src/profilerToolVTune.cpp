/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "profilerToolVTune.h"

//#define USE_SCRIPT_SAMPLES // creating string handles in VTune is quite heavy, so we do not want to use script samples by default

#ifdef USE_VTUNE_PROFILER

#pragma comment ( lib, "../../../../external/ittnotify/lib64/libittnotify.lib" )

#define USE_MULTIPLE_DOMAINS
#ifdef USE_MULTIPLE_DOMAINS
static const constexpr Bool c_useMultipleDomains = true;
#else
static const constexpr Bool c_useMultipleDomains = false;
#endif

VTuneProfilerTool::VTuneProfilerTool()
	: m_blockStrings()
	, m_domains()
	, m_frameDomain()
{

	if ( c_useMultipleDomains )
	{
		// Create own domain so always collected even if we want to disable the "None" profiler channel.
		m_frameDomain = __itt_domain_createA( "Frame" );

		for ( Uint32 i = 0; i < PBC_COUNT; ++i )
		{
			const char* domainName = GetProfilerBlockChannelName( (EProfilerBlockChannel)i );
			if ( i == 0 )
			{
				domainName = "Miscellaneous"; // vs confusing "None".
			}
			m_domains[ i ] = __itt_domain_createA( domainName );
		}
	}
	else
	{
		const auto domain = __itt_domain_createA( "Game" );
		m_frameDomain = domain;
		for ( Uint32 i = 0; i < PBC_COUNT; ++i )
		{
			m_domains[ i ] = domain;	
		}
	}

	if ( c_useMultipleDomains )
	{
		// Can enable only what you want to collect events for here
		red::StaticArray< EProfilerBlockChannel, PBC_COUNT > enabledChannels;
// 	 	enabledChannels.PushBack( PBC_STREAMING );
// 	 	enabledChannels.PushBack( PBC_IO );
		if ( !enabledChannels.Empty() )
		{
 			for ( Uint32 i = 0; i < PBC_COUNT; ++i )
 			{
				if ( !enabledChannels.Exist( (EProfilerBlockChannel)i ) )
				{
 					m_domains[ i ]->flags = 0;
				}
 			}
		}
	}
}

void VTuneProfilerTool::Init( const Uint32 mem )
{
	RED_UNUSED(mem);
}

void VTuneProfilerTool::InitThread( const char* threadName )
{
	__itt_thread_set_nameA( threadName );
}

void VTuneProfilerTool::InitBlock( red::InstrumentationObject* block, const char* scopeName )
{
	// Cache the string handle; creating string handles is quite heavy, even though the walkthroughs and API heavily suggest they wouldn't be.
	// #tbd: could store in the block itself as extra data, but then affecting all other profiling

#ifndef USE_SCRIPT_SAMPLES
	if ( red::Strstr( scopeName, "[SCRIPT]" ) )
	{
		return;
	}
#endif

	m_blockStrings[ block->m_id] = __itt_string_handle_createA( scopeName );
}

void VTuneProfilerTool::EmitGlobalMarker( red::InstrumentationObject* block )
{
	__itt_marker( m_domains[0], __itt_null, m_blockStrings[ block->m_id ], __itt_marker_scope_global );
}

void VTuneProfilerTool::NextFrame( red::ProfilerFrameType frameType )
{
	if ( frameType == red::ProfilerFrameType::PFT_ENGINE )
	{
		// Consecuitive begins are treated as __itt_frame_end_v3/__itt_frame_begin_v3 pairs
		__itt_frame_begin_v3( m_frameDomain, nullptr );
	}
}

void VTuneProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	auto* const taskName = m_blockStrings[ block->m_id ];
	__itt_task_begin( m_domains[ block->m_forceChannel ], __itt_null, __itt_null, taskName );
}

void VTuneProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	RED_UNUSED( scopeName );
	__itt_task_end( m_domains[ block->m_forceChannel ] );
}


#else
RED_NO_EMPTY_FILE();
#endif