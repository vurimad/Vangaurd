/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderParticleEmitterLOD.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( EParticleSortingMode, rend );
{
	RTTI_ENUM_OPTION( PSM_None );
	RTTI_ENUM_OPTION( PSM_Billboard );
	RTTI_ENUM_OPTION( PSM_Regular );
}
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ParticleBurst, rend );
RTTI_PROPERTY( m_burstTime );
RTTI_PROPERTY( m_spawnCount );
RTTI_PROPERTY( m_spawnTimeRange );
RTTI_PROPERTY( m_repeatTime );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( EmitterDurationSettings, rend );
RTTI_PROPERTY( m_emitterDuration );
RTTI_PROPERTY( m_emitterDurationLow );
RTTI_PROPERTY( m_useEmitterDurationRange );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( EmitterDelaySettings, rend );
RTTI_PROPERTY( m_emitterDelay );
RTTI_PROPERTY( m_emitterDelayLow );
RTTI_PROPERTY( m_useEmitterDelayRange );
RTTI_PROPERTY( m_useEmitterDelayOnce );
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( EmitterLOD, rend );
RTTI_PROPERTY( m_lodSwitchDistance );
RTTI_PROPERTY( m_burstList );
RTTI_PROPERTY( m_birthRate );
RTTI_PROPERTY( m_emitterDurationSettings );
RTTI_PROPERTY( m_emitterDelaySettings );
RTTI_PROPERTY( m_sortingMode );
RTTI_PROPERTY( m_isEnabled );
RTTI_END_TYPE();

