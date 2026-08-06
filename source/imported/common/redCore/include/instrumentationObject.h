#pragma once

#include "../../redSystem/include/hash.h"
#include "../../redMath/include/redMathPublic.h"
#include "redCoreApi.h"
#include "profilerChannels.h"

namespace red
{
	struct InstrumentationObject;

	REDCORE_API math::Color SelectMarkerColor( const InstrumentationObject* block, const char* name );
	REDCORE_API EProfilerBlockChannel GetCurrentForcedChannel();

    struct InstrumentationObject
    {
		RED_USE_MEMORY_POOL( red::PoolDebug );

		explicit InstrumentationObject( const char* name = nullptr,  EProfilerBlockChannel forceChannel = PBC_NONE ) : 
			m_name( name ),
			m_forceChannel( forceChannel != PBC_NONE ? forceChannel : GetCurrentForcedChannel() ),
			m_id( 0 ),
			m_profilerColor( red::CalculateAnsiHash32( name ) ),
			m_breakpointAtExecTime( 0.0f ),
			m_breakpointAtHitCount( 0 ),
			m_enabled( true ),
			m_registered( false ),
			m_breakOnce( false ),
			m_onceStopped( false )
        {
        }

        ~InstrumentationObject()
		{}	

        RED_ALIGN( 16 ) const char* m_name;
		EProfilerBlockChannel m_forceChannel;
		EProfilerBlockChannel m_prevChannel;
		Uint32				m_id;
		Uint32				m_profilerColor;
		Float				m_breakpointAtExecTime;
		red::Atomic<Uint32> m_breakpointAtHitCount;
		red::Atomic<Bool>	m_enabled;
		Bool				m_registered;
		Bool				m_breakOnce;
		red::Atomic<Bool>   m_onceStopped;
    };
}
