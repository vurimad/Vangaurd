/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../particles/include/evaluator.h"

namespace rend
{
	template< typename PARAM_TYPE > // Vector2, Vector3, Vector4, Float
	struct SEvaluatorParam
	{
		Uint16		 m_numElements;
		Uint16		 m_size;
		PARAM_TYPE   m_samples[MAX_NUM_CURVE_SAMPLES];
	};

	// SParticleUpdaterData
	struct RENDER_DATA_API RenderParticleUpdaterData
	{
		RED_USE_MEMORY_POOL( rend::PoolParticleData );

		RTTI_DECLARE_TYPE( RenderParticleUpdaterData );

	public:
		DataBuffer	m_data{ rend::PoolParticleData() };			//! Raw data of the particle updater initializers
		Uint32		m_modifOffset;	//! Byte offset to modificators segment of the updater data

		RED_INLINE Uint32 GetModificatorsOffset() const { return m_modifOffset; }
		RED_INLINE Uint32 GetModificatorsSize() const { return m_data.Size() - m_modifOffset; }
		RED_INLINE Uint32 GetInitializersSize() const { return m_modifOffset; }
		RED_INLINE Uint8* GetData() { return static_cast<Uint8*>( m_data.Data() ); }
		RED_INLINE const Uint8* GetData() const { return static_cast<const Uint8*>( m_data.Data() ); }

		RED_INLINE void Clear()
		{
			m_data.Clear();
			m_animFrameInit.Clear();
			m_modifOffset = 0;
		}

		/************************** CPU ***************************/
		/*	Those getters are only used by CPU emitters			  */
		/*  and you shouldn't change that if you don't want to    */
		/*  break GPU particles.								  */
		/**********************************************************/

		/* ACHTUNG!!! - The order in which parameters are set matters! Make sure you read them in the same order in initializer / modificator functors. */
		template< typename PARAM_TYPE >
		RED_INLINE const SEvaluatorParam< PARAM_TYPE >* GetEvaluatorParam( Uint32& ptrOffset ) const
		{
			const SEvaluatorParam< PARAM_TYPE >* ret = (SEvaluatorParam< PARAM_TYPE >*)( GetData() + ptrOffset );
			ptrOffset += ret->m_size;
			return ret;
		}

		template< typename PARAM_TYPE >
		RED_INLINE const PARAM_TYPE GetParam( Uint32& ptrOffset ) const
		{
			const PARAM_TYPE* ret = (PARAM_TYPE*)( GetData() + ptrOffset );
			ptrOffset += sizeof(PARAM_TYPE);
			return *ret;
		}

	public:
		// Common initializers
		red::DynArray< Float >			m_animFrameInit{ rend::PoolParticleData() };

		// Common modifiers
		Float							m_turbulenceNoiseInterval;
		Float							m_turbulenceDuration;

		Uint64							m_collisionMask;
		Float							m_collisionDynamicFriction;
		Float							m_collisionStaticFriction;
		Float							m_collisionRestitution;
		Float							m_collisionVelocityDamp;
		Bool							m_collisionDisableGravity;
		Float							m_collisionRadius;
		Uint32							m_collisionEffectMask;
		Uint8							m_maxCollisions;

		CName							m_eventGenerate;
		CName							m_eventReceive;
		Float							m_eventFrequency;
		Float							m_eventProbability;
		Uint8							m_eventSpawnObject;

		Uint8							m_noiseType;
		Bool								m_randomPerChannel;

		RenderParticleUpdaterData( Bool gpu = false )
			: m_modifOffset( 0 )
			, m_turbulenceNoiseInterval( 0.0f )
			, m_turbulenceDuration( 0.0f )
			, m_collisionMask( 0 )
			, m_collisionDynamicFriction( 0.0f )
			, m_collisionStaticFriction( 0.0f )
			, m_collisionRestitution( 0.0f )
			, m_collisionVelocityDamp( 0.0f )
			, m_collisionDisableGravity( false )
			, m_collisionRadius( 0.1f )
			, m_collisionEffectMask( 0 )
			, m_maxCollisions( 6 )
			, m_eventFrequency( 1.0f )
			, m_eventProbability( 1.0f )
			, m_eventSpawnObject( 0 )
			, m_noiseType( 0 )
			, m_randomPerChannel( true )
		{
		}

		RenderParticleUpdaterData( const RenderParticleUpdaterData& other )
			: m_data( DataBuffer::Copy( other.m_data, rend::PoolParticleData() ) )
			, m_modifOffset( other.m_modifOffset )
			, m_animFrameInit( other.m_animFrameInit )
			, m_turbulenceNoiseInterval( other.m_turbulenceNoiseInterval )
			, m_turbulenceDuration( other.m_turbulenceDuration )
			, m_collisionMask( other.m_collisionMask )
			, m_collisionDynamicFriction( other.m_collisionDynamicFriction )
			, m_collisionStaticFriction( other.m_collisionStaticFriction )
			, m_collisionRestitution( other.m_collisionRestitution )
			, m_collisionVelocityDamp( other.m_collisionVelocityDamp )
			, m_collisionDisableGravity( other.m_collisionDisableGravity )
			, m_collisionRadius( other.m_collisionRadius )
			, m_collisionEffectMask( other.m_collisionEffectMask )
			, m_maxCollisions( other.m_maxCollisions )
			, m_eventGenerate( other.m_eventGenerate )
			, m_eventReceive( other.m_eventReceive )
			, m_eventFrequency( other.m_eventFrequency )
			, m_eventProbability( other.m_eventProbability )
			, m_eventSpawnObject( other.m_eventSpawnObject )
			, m_noiseType( other.m_noiseType )
			, m_randomPerChannel( other.m_randomPerChannel )
		{
		}

		virtual ~RenderParticleUpdaterData()
		{
		}

		RenderParticleUpdaterData& operator=( const RenderParticleUpdaterData& other )
		{
			if ( this != &other )
			{
				m_data = DataBuffer::Copy( other.m_data, rend::PoolParticleData() );
				m_animFrameInit = other.m_animFrameInit;
				m_modifOffset = other.m_modifOffset;
				m_turbulenceNoiseInterval = other.m_turbulenceNoiseInterval;
				m_turbulenceDuration = other.m_turbulenceDuration;
				m_collisionMask = other.m_collisionMask;
				m_collisionDynamicFriction = other.m_collisionDynamicFriction;
				m_collisionStaticFriction = other.m_collisionStaticFriction;
				m_collisionRestitution = other.m_collisionRestitution;
				m_collisionVelocityDamp = other.m_collisionVelocityDamp;
				m_collisionDisableGravity = other.m_collisionDisableGravity;
				m_collisionRadius = other.m_collisionRadius;
				m_collisionEffectMask = other.m_collisionEffectMask;
				m_maxCollisions = other.m_maxCollisions;
				m_eventGenerate = other.m_eventGenerate;
				m_eventReceive = other.m_eventReceive;
				m_eventFrequency = other.m_eventFrequency;
				m_eventProbability = other.m_eventProbability;
				m_eventSpawnObject = other.m_eventSpawnObject;
				m_noiseType = other.m_noiseType;
				m_randomPerChannel = other.m_randomPerChannel;
			}
			return *this;
		}
	};
}