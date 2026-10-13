/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderParticleBlob.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( EmitterSimulationShaders, rend );
{
	RTTI_PROPERTY( m_simCS );
}
RTTI_END_TYPE();


rend::EmitterSimulationShaders::EmitterSimulationShaders()
{
}

const Uint32 rend::EmitterSimulationShaders::CalcSize() const
{
	Uint32 ret = 0;
	for(Uint32 i = 0; i < ESP_Max; ++i)
	{
		ret += m_simCS[i].Size();
	}
	return ret;
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderParticleBlobEmitterInfo, rend );
{
	RTTI_PROPERTY( m_emitterHash );
	RTTI_PROPERTY( m_diffuseWrapFactor );
	RTTI_PROPERTY( m_backLightingFactor );
	RTTI_PROPERTY( m_lightingMipBias );
	RTTI_PROPERTY( m_maskInsideCar );
	RTTI_PROPERTY( m_maskInsideInterior );
	RTTI_PROPERTY( m_maskAboveWater );
	RTTI_PROPERTY( m_maskUnderWater );
	RTTI_PROPERTY( m_maxParticles );
	RTTI_PROPERTY( m_emitterLoops );
	RTTI_PROPERTY( m_internalPriority );
	RTTI_PROPERTY( m_keepSimulationLocal );
	RTTI_PROPERTY( m_killOnCollision );
	RTTI_PROPERTY( m_initialParticleCount );
	RTTI_PROPERTY( m_useSubFrameEmission );
	RTTI_PROPERTY( m_windInfluence );
	RTTI_PROPERTY( m_particleType );
	RTTI_PROPERTY( m_vertexDrawerType );
	RTTI_PROPERTY( m_simulationType );
	RTTI_PROPERTY( m_envColorGroup );
	RTTI_PROPERTY( m_emitterGroup );
	RTTI_PROPERTY( m_renderObjectType );
	RTTI_PROPERTY( m_numModifiers );
	RTTI_PROPERTY( m_modifierSetMask );
	RTTI_PROPERTY( m_numInitializers );
	RTTI_PROPERTY( m_initializerSetMask );
	RTTI_PROPERTY( m_simulationHash );
	RTTI_PROPERTY( m_eventSetMask );
	RTTI_PROPERTY( m_seeds );
	RTTI_PROPERTY( m_lods );
	RTTI_PROPERTY( m_volumetricParticleEnabled );
	RTTI_PROPERTY( m_volumetricParticleRelative );
	RTTI_PROPERTY( m_volumetricParticleUseFogColor );
	RTTI_PROPERTY( m_volumetricParticleColor );
	RTTI_PROPERTY( m_volumetricParticleSize );
	RTTI_PROPERTY( m_volumetricParticleDensity );
	RTTI_PROPERTY( m_volumetricParticleFalloff );
	RTTI_PROPERTY( m_volumetricParticleNoiseScale );
	RTTI_PROPERTY( m_volumetricParticleNoiseThreshold );
	RTTI_PROPERTY( m_volumetricParticleNoiseVelocity );
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderParticleBlobHeader, rend );
{
	RTTI_PROPERTY( m_version );
	RTTI_PROPERTY( m_emitterInfo );
}
RTTI_END_TYPE();


RTTI_BEGIN_TYPE_IN_NAMESPACE( RenderParticleBlob, rend );
{
	RTTI_PARENT_TYPE( IRenderResourceBlob );
	RTTI_PROPERTY( m_header );
	RTTI_PROPERTY( m_updaterData );
	RTTI_PROPERTY( m_gpuSimShaders );
}
RTTI_END_TYPE();


rend::RenderParticleBlob::RenderParticleBlob()
{
	red::Memzero( &m_header, sizeof( m_header ) );
}

rend::RenderParticleBlob::~RenderParticleBlob()
{
}

Uint32 rend::RenderParticleBlob::GetVersion() const
{
	return m_header.m_version;
}

Bool rend::RenderParticleBlob::IsVersionCurrent() const
{
	return m_header.m_version == CURRENT;
}
Bool rend::RenderParticleBlob::IsVersionValid() const
{
	return m_header.m_version >= MIN_SUPPORTED && m_header.m_version <= CURRENT; // To support opening files after reverting
}

Uint32 rend::RenderParticleBlob::GetDataSize() const
{
	return m_gpuSimShaders.CalcSize() + m_updaterData.m_data.Size();
}