/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderResourceBlob.h"
#include "particleEmitterEnum.h"
#include "renderParticleUpdaterData.h"
#include "renderParticleEmitterLOD.h"
#include "renderPublicEnums.h"

#include "../../redReflection/include/deferredDataBuffer.h"




namespace rend
{
	enum ParticleFileVersion
	{
		DUMMY0,
		DUMMY1,
		DUMMY2,
		LOCAL_TRANSFORM_SPHERE_SPAWNER,
		LOCAL_TRANSFORM_VELOCITY_TURBULIZE,
		SPHERIZE_AND_BACKLIGHTING_EMITTER_PARAMETERS,
		SPAWN_BOX_SURFACE_ONLY,
		NON_DYNAMIC_TRAIL_WORLD_SPACE_TEXCOORD,
		MASK_INSIDE_CAR_EMITTER_PARAMETER,
		MASK_INSIDE_INTERIOR_EMITTER_PARAMETER,
		MASK_ABOVE_WATER_EMITTER_PARAMETER,
		MASK_UNDER_WATER_EMITTER_PARAMETER,
		LIGHTING_MIP_BIAS_PARAMETER,
		CURRENT = LIGHTING_MIP_BIAS_PARAMETER, // Latest version, update if you have changed particle binary data (e.g., added parameter)
		// Delete any versions < PARTICLES_MIN_SUPPORTED_VERSION when you bump the latter
		MIN_SUPPORTED = 0, // Resources compiled with this version below this one will not be loaded
	};

	struct EmitterSimulationShaders
	{
		RTTI_DECLARE_TYPE( EmitterSimulationShaders );

	public:
		DataBuffer	m_simCS[ESP_Max];	// Gpu Simulation shaders

		EmitterSimulationShaders();

		// calculate size of the cooked data
		const Uint32 CalcSize() const;

		// Clear specific buffers
		RED_INLINE void ClearDataBuffers()
		{
			for( Uint32 i = 0; i < ESP_Max; ++i )
			{
				m_simCS[i].Clear();
			}
		}
	};

	struct RenderParticleBlobEmitterInfo
	{
		RTTI_DECLARE_TYPE( RenderParticleBlobEmitterInfo );

		Uint64							m_emitterHash{0};			// hash
		Float							m_diffuseWrapFactor{0.0f};
		Float							m_backLightingFactor{0.0f};
		Uint32							m_lightingMipBias{0};
		Uint32							m_maxParticles{0};
		Int8							m_emitterLoops{0};
		Uint8							m_internalPriority{0};
		Bool							m_maskInsideCar{false};
		Bool							m_maskInsideInterior{ false };
		Bool							m_maskAboveWater{ false };
		Bool							m_maskUnderWater{ false };
		Bool							m_keepSimulationLocal{ false };
		Bool							m_killOnCollision{ false };
		Uint8							m_initialParticleCount{0};
		Bool							m_useSubFrameEmission{ false };
		Float							m_windInfluence{0.0f};
		Uint32							m_particleType{0};
		Uint32							m_vertexDrawerType{0};
		Uint32							m_simulationType{0};
		Uint32							m_envColorGroup{0};
		Uint32							m_emitterGroup{0};
		ERenderObjectType				m_renderObjectType{ ROT_Particle };

		Uint32							m_numModifiers{0};
		Uint32							m_numInitializers{0};
		Uint64							m_modifierSetMask{0};
		Uint64							m_initializerSetMask{0};
		Uint64							m_simulationHash{0};
		Uint16							m_eventSetMask{0};
		red::DynArray< Uint32 >			m_seeds{ rend::PoolRenderBlobs() };
		red::DynArray< EmitterLOD >		m_lods{ rend::PoolRenderBlobs() };

		Bool							m_volumetricParticleEnabled{false};
		Bool							m_volumetricParticleRelative{ false };
		Bool							m_volumetricParticleUseFogColor{ false };
		HDRColor						m_volumetricParticleColor{HDRColor::WHITE()};
		Float							m_volumetricParticleSize{0.0f};
		Float							m_volumetricParticleDensity{0.0f};
		Float							m_volumetricParticleFalloff{0.0f};
		Float							m_volumetricParticleNoiseScale{0.0f};
		Float							m_volumetricParticleNoiseThreshold{0.0f};
		Vector3							m_volumetricParticleNoiseVelocity{Vector3::ZEROS()};
		Float							m_collisionAngularVelocityDamp{0.0f};
	};
	using EmitterInfo = RenderParticleBlobEmitterInfo;

	struct RenderParticleBlobHeader
	{
		RTTI_DECLARE_TYPE( RenderParticleBlobHeader );
		
		Uint32				m_version;		 // version of the format
		EmitterInfo			m_emitterInfo;	 // emitter info
	};
	
	class RENDER_DATA_API RenderParticleBlob : public IRenderResourceBlob
	{
		RTTI_DECLARE_TYPE( RenderParticleBlob );

	public:
		typedef RenderParticleBlobHeader Header;

	public:
		RenderParticleBlob();
		~RenderParticleBlob();

		RED_INLINE virtual ERenderResourceType GetType() const override { return RRT_Particle; }

		virtual Uint32 GetVersion() const override;

		virtual Bool IsVersionCurrent() const override;
		virtual Bool IsVersionValid() const override;

		virtual Uint32 GetDataSize() const override;

		RED_INLINE Uint64 GetHash() const { return m_header.m_emitterInfo.m_emitterHash; }
	
		Header						m_header;
		RenderParticleUpdaterData	m_updaterData;		//!< Updater data blob
		EmitterSimulationShaders	m_gpuSimShaders;	//!< Gpu sim shaders
	};
}//rnd