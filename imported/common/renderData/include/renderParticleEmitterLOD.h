/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace rend
{
	enum EParticleSortingMode
	{
		PSM_None,
		PSM_Billboard,
		PSM_Regular
	};

	/// Particle burst
	struct ParticleBurst
	{
		RTTI_DECLARE_TYPE( ParticleBurst );

		Float		m_burstTime;			//!< When to fire the burst
		Uint32		m_spawnCount;			//!< How many particles to spawn at this burst
		Float		m_spawnTimeRange;		//!< Spawned particles are assumed to be spawned during this period of time
		Float		m_repeatTime;			//!< Time interval between bursts

		RED_INLINE ParticleBurst()
			: m_burstTime( 0.0f )
			, m_spawnCount( 1 )
			, m_spawnTimeRange( 0.0f )
			, m_repeatTime( 0.0f )
		{};
	};

	/// Emitter Duration
	struct EmitterDurationSettings
	{
		RTTI_DECLARE_TYPE( EmitterDurationSettings );

		Float		m_emitterDuration;			//!< Duration for emitter
		Float		m_emitterDurationLow;		//!< Used with Duration range, lowest emitter duration time 
		Bool		m_useEmitterDurationRange;	//!< Randomized emitter duration

		RED_INLINE EmitterDurationSettings()
			: m_emitterDuration( 1.0f )
			, m_emitterDurationLow( 0.0f )
			, m_useEmitterDurationRange( false )
		{};
	};

	/// Emitter Delay
	struct RENDER_DATA_API EmitterDelaySettings
	{
		RTTI_DECLARE_TYPE( EmitterDelaySettings );

		Float		m_emitterDelay;			//!< Delay before the emitter starts
		Float		m_emitterDelayLow;		//!< Used with Delay Range to have a random delay
		Bool		m_useEmitterDelayRange;	//!< Randomized delay of particles
		Bool		m_useEmitterDelayOnce;	//!< Delay the particles spawn just once

		RED_INLINE EmitterDelaySettings()
			: m_emitterDelay( 0.0f )
			, m_emitterDelayLow( 0.0f )
			, m_useEmitterDelayRange( false )
			, m_useEmitterDelayOnce( false )
		{};
	};

	struct RENDER_DATA_API EmitterLOD
	{
		RTTI_DECLARE_TYPE( EmitterLOD );

		float							m_lodSwitchDistance = 100.0f;	//!< Distance at which the next lod is used
		red::DynArray< ParticleBurst >	m_burstList{ rend::PoolParticleData() };				//!< List of emission bursts
		red::DynArray< Float >			m_birthRate{ rend::PoolParticleData() };				//!< Rate of spawn
		EmitterDurationSettings			m_emitterDurationSettings;		//!< Duration when spawning particles
		EmitterDelaySettings			m_emitterDelaySettings;			//!< Delay before spawning particles

		EParticleSortingMode			m_sortingMode;					//!< Sort particles every frame

		Bool							m_isEnabled;					//!< Is the emitter enabled at this LOD?

		//! Get emitter duration
		RED_INLINE Float GetEmitterDuration() const { return m_emitterDurationSettings.m_emitterDuration; }
		RED_INLINE Float GetEmitterDurationLow() const { return m_emitterDurationSettings.m_emitterDurationLow; }
		RED_INLINE Bool  UseEmitterDurationRange() const { return m_emitterDurationSettings.m_useEmitterDurationRange; }

		RED_INLINE Float GetLODSwitchDistance() const { return m_lodSwitchDistance; }

		RED_INLINE Float GetEmitterDelay() const { return m_emitterDelaySettings.m_emitterDelay; }
		RED_INLINE Float GetEmitterDelayLow() const { return m_emitterDelaySettings.m_emitterDelayLow; }
		RED_INLINE Bool  UseEmitterDelayRange() const { return m_emitterDelaySettings.m_useEmitterDelayRange; }
		RED_INLINE Bool  UseEmitterDelayOnce() const { return m_emitterDelaySettings.m_useEmitterDelayOnce; }
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( EParticleSortingMode, rend );