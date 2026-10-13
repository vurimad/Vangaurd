/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "profilerToolPix.h"

#ifdef USE_PIX_PROFILER

#define USE_PIX

#if defined( RED_PLATFORM_DURANGO )
#include "pix.h"
#elif defined( RED_PLATFORM_WINPC )
# include <pix3.h>
#else
# error Unsupported PIX platform!
#endif

#define USE_CHANNEL_COLOR_CODING
#ifdef USE_CHANNEL_COLOR_CODING
static const constexpr Bool c_useChannelColorCoding = true;
#else
static const constexpr Bool c_useChannelColorCoding = false;
#endif

RED_FORCE_INLINE static Uint32 SelectPIXMarkerColor( const red::InstrumentationObject* __restrict block )
{
	// #todo: can't use lzcnt on PC guaranteed, an the BSR could branch, but shouldn't super matter
	return PIX_COLOR( 0xFF, 0, 0 );
}

void PixProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
{
	const Uint32 color = SelectPIXMarkerColor( block );
	PIXBeginEvent( color, scopeName );
}

void PixProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
{
	RED_UNUSED( block );
	RED_UNUSED( scopeName );
	PIXEndEvent();
}

#else
	RED_NO_EMPTY_FILE();
#endif
