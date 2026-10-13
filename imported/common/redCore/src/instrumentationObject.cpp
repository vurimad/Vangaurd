/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "instrumentationObject.h"
#include "profilerChannels.h"
#include "../../redMath/include/redMathPublic.h"

namespace red
{
	/*
	Using c_useChannelColorCoding to avoid code-rot of def'd out code.
	When it's false, the compiler will optimize SelectMarkerColor out and
	simply call StartBlock like this with SCE_RAZOR_CPU_COLOR_RED (800000FFh)
	mov          esi,800000FFh
	xor          edx,edx
	mov          rdi,rax
	jmp          sceRazorCpuPushMarker
	*/
#define USE_CHANNEL_COLOR_CODING
#ifdef USE_CHANNEL_COLOR_CODING
	static const constexpr Bool c_useChannelColorCoding = true;
#else
	static const constexpr Bool c_useChannelColorCoding = false;
#endif

	static thread_local EProfilerBlockChannel t_perfChannel = PBC_NONE;

	// Can force channels because lots of places use just a PC_SCOPE marker,
	// which makes it harder to keep track of what's rendering vs CPU
	REDCORE_API EProfilerBlockChannel SwapThreadLocalPerfChannels( EProfilerBlockChannel newChannels )
	{
		if (c_useChannelColorCoding)
		{
			EProfilerBlockChannel oldChannels = t_perfChannel;
			t_perfChannel = newChannels;
			return oldChannels;
		}
		return PBC_NONE;
	}

	static const math::Color c_colorsLUT[] =
	{
		math::Color::BLUE(),		//PBC_NONE = 0,
		math::Color::YELLOW(),		//PBC_JOB,	
		math::Color::BROWN(),		//PBC_IO,
		math::Color::GREEN(),		//PBC_PHYSX,
		math::Color::RED(),			//PBC_RENDER,
		math::Color::ORANGE(),		//PBC_ANIMATION
		math::Color::MAGENTA(),		//PBC_AUDIO
		math::Color::BLUE(),		//PBC_STREAMING
		math::Color::LIGHT_CYAN(),	//PBC_UI
		math::Color::BLUE(),		//PBC_SPAWNING
		math::Color::BLUE(),		//PBC_RUNTIMESYSTEM
		math::Color::BLUE(),		//PBC_LOADINGFENCE
		math::Color::GRAY(),		//PBC_SCRIPTS
		math::Color::BLACK()		//PBC_BUCKET
	};
	static_assert( sizeof( c_colorsLUT ) / sizeof( c_colorsLUT[ 0 ] ) == PBC_COUNT, "Some channels don't have a color assigned." );

	REDCORE_API math::Color SelectMarkerColor( const InstrumentationObject* block, const char* name )
	{
		if (c_useChannelColorCoding)
		{
			// Tread the channel directly as an index into the color LUT
			const Uint32 colorIndex = t_perfChannel;

			// Tint the scope color with the channel color for easier readability of the capture
			return math::Color::Lerp( 0.2f, c_colorsLUT[ colorIndex ], math::Color( block->m_profilerColor ) );

		}
		return math::Color::RED();
	}

	EProfilerBlockChannel GetCurrentForcedChannel()
	{
		return t_perfChannel;
	}
}