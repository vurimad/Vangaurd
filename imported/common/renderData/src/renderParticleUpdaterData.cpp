/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderParticleUpdaterData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderParticleUpdaterData, rend );
RTTI_PROPERTY( m_data );
RTTI_PROPERTY( m_modifOffset );
RTTI_PROPERTY( m_animFrameInit );
RTTI_PROPERTY( m_turbulenceNoiseInterval );
RTTI_PROPERTY( m_turbulenceDuration );
RTTI_PROPERTY( m_collisionMask );
RTTI_PROPERTY( m_collisionDynamicFriction );
RTTI_PROPERTY( m_collisionStaticFriction );
RTTI_PROPERTY( m_collisionRestitution );
RTTI_PROPERTY( m_collisionVelocityDamp );
RTTI_PROPERTY( m_collisionDisableGravity );
RTTI_PROPERTY( m_collisionRadius );
RTTI_PROPERTY( m_collisionEffectMask );
RTTI_PROPERTY( m_maxCollisions );
RTTI_PROPERTY( m_eventGenerate );
RTTI_PROPERTY( m_eventReceive );
RTTI_PROPERTY( m_eventFrequency );
RTTI_PROPERTY( m_eventProbability );
RTTI_PROPERTY( m_noiseType );
RTTI_PROPERTY( m_randomPerChannel );
RTTI_PROPERTY( m_eventSpawnObject );
RTTI_END_TYPE();

