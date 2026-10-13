/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderObjectPtr.h"
#include "renderResource.h"
#include "envParameters.h"

struct SParticleEffectContextUpdate
{
	Vector4	 						m_targetTranslation;		//!< Simulation target translation
	Float							m_globalEmissionScale;		//!< Global emission scale
	Float							m_effectAlpha;				//!< Alpha value override by effect
	Float							m_effectSize;				//!< Size value multiplier
	Float							m_effectVelocityScale;		//!< Velocity value multiplier
	Float							m_timeMultiplier;			//!< Size value multiplier
	Vector3							m_windVector;				//!< Wind vector
	Vector3							m_windVectorOnly;			//!< Wind vector without

	SParticleEffectContextUpdate()
		: m_timeMultiplier( 1.0f )
	{}
};

//This struct is written to by physics and read by renderer
struct SParticlePhysicsSimulationData
{
	RED_USE_MEMORY_POOL( rend::PoolParticleData );

	static constexpr Uint32 s_maxPhysicalParticles = 128;

	red::BitSet<s_maxPhysicalParticles> m_collided;
	red::FixedArray<Vector3, s_maxPhysicalParticles> m_positions;
	red::FixedArray<Vector3, s_maxPhysicalParticles> m_velocities;
};

struct SParticlePhysicsSimulationDataDoubleBuffered
{
	RED_USE_MEMORY_POOL( rend::PoolParticleData );

	SParticlePhysicsSimulationData buffer[2];
	red::Atomic<Uint8> activeIdx;

	SParticlePhysicsSimulationDataDoubleBuffered()
		: activeIdx(0)
	{}

	void Swap() 
	{ 
		activeIdx.SetValue(1 - activeIdx.GetValue());
	}

	SParticlePhysicsSimulationData& GetActive() 
	{ 
		return buffer[activeIdx.GetValue()];
	}

	SParticlePhysicsSimulationData& GetInactive()
	{ 
		return buffer[1 - activeIdx.GetValue()]; 
	}

	const SParticlePhysicsSimulationData& GetActive() const
	{ 
		return buffer[activeIdx.GetValue()];
	}

	const SParticlePhysicsSimulationData& GetInactive() const
	{ 
		return buffer[1 - activeIdx.GetValue()]; 
	}
};

RED_INLINE void DefaultOnAllocStub( Float ){}
RED_INLINE void DefaultOnFreeStub(Uint32, Uint32) {}

//This struct is written to by renderer and read by physics
struct SParticleEmitterSimulationData
{
	RED_USE_MEMORY_POOL( rend::PoolParticleData );

	using FnAllocParticle = red::FixedSizeFunction<void( Float )>;
	using FnFreeParticle = red::FixedSizeFunction<void( Uint32, Uint32 )>;

	FnAllocParticle m_onAlloc = DefaultOnAllocStub;
	FnFreeParticle  m_onFree  = DefaultOnFreeStub;

	//think about adding a lock here
	red::RWSpinLock m_lock;

	Uint32 nbParticles = 0u;

	//!< Needed by some of the initializers
	Vector4 cameraPosition = Vector4::PLUS_INF();
	Vector4 emitterVelocity = Vector4::PLUS_INF();
};

struct SFogVolumeEffectContextUpdate
{
	Float	m_density;
	Vector4	m_size;
	Vector4	m_color;

	SFogVolumeEffectContextUpdate()
	{}
};

struct SColorGradeEffectContextUpdate
{
	Float m_saturate;
	Float m_contrast;
	Float m_brightness;
	Float m_lutWeight;
	ColorGradingResolvedLutParams m_sdrLutParams;
	ColorGradingResolvedLutParams m_hdrLutParams;

	SColorGradeEffectContextUpdate()
		: m_saturate( 0.0f )
		, m_brightness( 0.0f )
		, m_contrast( 0.0f )
		, m_lutWeight( 0.0f )
	{}
};

struct SAudioCurveEffectContextUpdate
{
	Float m_value;

	SAudioCurveEffectContextUpdate()
		: m_value( 0.0f )
	{}
};

struct SMaterialParameterEffectContextUpdate
{
	Vector4 m_parameters[7];
	Uint8	m_paramMask;

	SMaterialParameterEffectContextUpdate()
		: m_paramMask( 0 )
	{
		m_parameters[0] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[1] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[2] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[3] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[4] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[5] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
		m_parameters[6] = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
	}
};

struct SLightParameterEffectContextUpdate
{
	// @note if you add more, update HasOverride bellow!
	Bool  m_overrideIntensity : 1;
	Bool  m_overrideIntensityMultiplier : 1;
	Bool  m_overrideRadius : 1;
	Bool  m_overrideInnerAngle : 1;
	Bool  m_overrideOuterAngle : 1;
	Bool  m_overrideColor : 1;
	Float m_intensity;
	Float m_intensityMultiplier;
	Float m_radius;
	Float m_outerAngle;
	Float m_innerAngle;
	Color m_color;

	SLightParameterEffectContextUpdate()
		: m_overrideIntensity( false )
		, m_overrideIntensityMultiplier( false )
		, m_intensity( 100.0f )
		, m_intensityMultiplier( 1.0f )
		, m_overrideRadius( false )
		, m_radius( 5 )
		, m_overrideInnerAngle(false)
		, m_innerAngle (30.0f)
		, m_overrideOuterAngle(false)
		, m_outerAngle (45.0f)
		, m_overrideColor(false)
		, m_color (Color::BLACK())
	{
	}

	Bool HasOverrides() const 
	{ 
		return m_overrideIntensity 
			|| m_overrideIntensityMultiplier 
			|| m_overrideRadius 
			|| m_overrideInnerAngle 
			|| m_overrideOuterAngle
			|| m_overrideColor; 
	}
};

struct SLightFlickerParamsUpdate
{
	Float		m_positionOffset;
	Float		m_flickerStrength;
	Float		m_flickerPeriod;

	SLightFlickerParamsUpdate()
		: m_positionOffset(0.0f)
		, m_flickerStrength(0.2f)
		, m_flickerPeriod(0.0f)
	{
	}
};

struct SDecalEffectContextUpdate
{
	Vector4	m_scale;
	Float	m_alpha;
	Float	m_emissiveScale;
	Bool	m_updateScale;
	Bool	m_updateEmissiveScale;
	Bool	m_updateAlpha;

	SDecalEffectContextUpdate()
		: m_alpha( 1.0f )
		, m_scale( Vector4::ONES() )
		, m_emissiveScale( 1.0f )
		, m_updateScale( false )
		, m_updateEmissiveScale( false )
		, m_updateAlpha( true )
	{}

	RED_FORCE_INLINE Bool ShouldUpdate() const { return m_updateAlpha || m_updateScale || m_updateEmissiveScale; }
};

struct SDestructionRegionsEffectContextUpdate
{
	const red::DynArray<Float>& m_regionPlayTimes;
};

struct SVehicleParamsUpdate
{
	Vector3 m_meshPivotInGridSpace = Vector3::ZEROS();
	Vector3 m_dimensions = Vector3::ZEROS();
};
