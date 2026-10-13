/**
* Copyright (c) 2011 CD Projekt Red. All Rights Reserved.
*/
#pragma once

constexpr Int32 PFS_Velocity	= RED_FLAG( 1 );
constexpr Int32 PFS_Color		= RED_FLAG( 2 );
constexpr Int32 PFS_Size2D		= RED_FLAG( 3 );
constexpr Int32 PFS_Size3D		= RED_FLAG( 4 );
constexpr Int32 PFS_Frame		= RED_FLAG( 5 );
constexpr Int32 PFS_Rotation2D	= RED_FLAG( 6 );
constexpr Int32 PFS_Rotation3D	= RED_FLAG( 7 );
constexpr Int32 PFS_Turbulence	= RED_FLAG( 8 );
constexpr Int32 PFS_EmitterAxis	= RED_FLAG( 9 );
constexpr Int32 PFS_Orbit		= RED_FLAG( 10 );

/// Particle offsets
// WARNING: Particle structure is memory critical... structure has been organized to minimize gaps between members
// Additionally most used members are at the beginning.

#define DEFINE_PARTICLE_OFFSET_FIRST( name, type ) static constexpr Int32 name##_SIZE(){ return sizeof(type); } constexpr Int32 name = 0;
#define DEFINE_PARTICLE_OFFSET( name, type, previous ) static constexpr Int32 name##_SIZE() { return sizeof(type); } constexpr Int32 name = previous + previous##_SIZE();

// Particle fields offsets
DEFINE_PARTICLE_OFFSET_FIRST(PARTICLE_POSITION, Vector3);
DEFINE_PARTICLE_OFFSET(PARTICLE_LIFE, Float, PARTICLE_POSITION);
DEFINE_PARTICLE_OFFSET(PARTICLE_LIFE_SPAN_INV, Float, PARTICLE_LIFE);
DEFINE_PARTICLE_OFFSET(PARTICLE_VELOCITY, Vector3, PARTICLE_LIFE_SPAN_INV);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_VELOCITY, Vector3, PARTICLE_VELOCITY);
DEFINE_PARTICLE_OFFSET(PARTICLE_FRAME, Float, PARTICLE_BASE_VELOCITY);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_FRAME, Float, PARTICLE_FRAME);
DEFINE_PARTICLE_OFFSET(PARTICLE_COLOR, Vector3, PARTICLE_BASE_FRAME);
DEFINE_PARTICLE_OFFSET(PARTICLE_ALPHA, Float, PARTICLE_COLOR);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_COLOR, Vector3, PARTICLE_ALPHA);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_ALPHA, Float, PARTICLE_BASE_COLOR);
DEFINE_PARTICLE_OFFSET(PARTICLE_EVT_FREQ_ACUM, Float, PARTICLE_BASE_ALPHA);
DEFINE_PARTICLE_OFFSET(PARTICLE_ID, Uint32, PARTICLE_EVT_FREQ_ACUM);

// 2D
DEFINE_PARTICLE_OFFSET(PARTICLE_SIZE, Vector2, PARTICLE_ID);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_SIZE, Vector2, PARTICLE_SIZE);
DEFINE_PARTICLE_OFFSET(PARTICLE_ROTATION, Float, PARTICLE_BASE_SIZE);
DEFINE_PARTICLE_OFFSET(PARTICLE_ROTATION_RATE, Float, PARTICLE_ROTATION);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_ROTATION_RATE, Float, PARTICLE_ROTATION_RATE);
DEFINE_PARTICLE_OFFSET(PARTICLE_TURBULENCE, Vector3, PARTICLE_BASE_ROTATION_RATE);
DEFINE_PARTICLE_OFFSET(PARTICLE_TURBULENCE_COUNTER, Float, PARTICLE_TURBULENCE);
DEFINE_PARTICLE_OFFSET(PARTICLE_ORBIT, Vector3, PARTICLE_TURBULENCE_COUNTER);

// 3D
DEFINE_PARTICLE_OFFSET(PARTICLE_ROTATION3D, Vector4, PARTICLE_ID);
DEFINE_PARTICLE_OFFSET(PARTICLE_ROTATION_RATE3D, Vector4, PARTICLE_ROTATION3D);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_ROTATION_RATE3D, Vector4, PARTICLE_ROTATION_RATE3D);
DEFINE_PARTICLE_OFFSET(PARTICLE_SIZE3D, Vector3, PARTICLE_BASE_ROTATION_RATE3D);
DEFINE_PARTICLE_OFFSET(PARTICLE_BASE_SIZE3D, Vector3, PARTICLE_SIZE3D);

DEFINE_PARTICLE_OFFSET(PARTICLE_EMITTER_AXIS, Vector3, PARTICLE_LIFE_SPAN_INV);
DEFINE_PARTICLE_OFFSET(PARTICLE_LAST_EMITTER_AXIS, Vector3, PARTICLE_EMITTER_AXIS);

#undef PARTICLE_DEFINE_OFFSET_FIRST
#undef PARTICLE_DEFINE_OFFSET

struct SimpleParticle
{
	Vector3		m_position;			//!< (0 byte) Current particle position
	Float		m_life;				//!< (12 byte) Particle life
	Float		m_lifeSpanInv;		//!< (16 byte) Relative scale of particle life span ( 0.0f for dead particles )
	Vector3		m_velocity;			//!< (20 byte) Current particle velocity
	Vector3		m_baseVelocity;		//!< (32 byte) Initial velocity

	Float		m_frame;			//!< (44 byte) Current particle animation frame
	Float		m_baseFrame;		//!< (48 byte) initial particle animation frame
	Vector3		m_color;			//!< (52 byte) Current particle color
	Float		m_alpha;			//!< (64 byte) 
	Vector3		m_baseColor;		//!< (68 byte) Initial particle color
	Float		m_baseAlpha;		//!< (80 byte) 

	Float		m_eventFrequencyAcum;//!< (84 byte) Current particle event generation frequency accumulator
	Float		m_eventOccurenceCounter;//!< (88 byte) Current particle event occurrence counter

	Vector2		m_size;				//!< (92 byte) Current particle size
	Vector2		m_baseSize;			//!< (100 byte) Initial particle size

	Float		m_rotation;			//!< (108 byte) Current particle rotation
	Float		m_rotationRate;		//!< (112 byte) Current particle rotation speed
	Float		m_baseRotationRate;	//!< (116 byte) Initial particle rotation speed

	Vector3		m_turbulence;		//!< (120 byte) Current particle turbulence
	Float		m_turbulenceCounter;//!< (132 byte) Current particle turbulence timeout

	Vector3		m_orbitVelocity;	//! (136 byte) Current particle orbit offset

	static const Int32 m_fieldSet = PFS_Velocity | PFS_Color | PFS_Size2D | PFS_Frame | PFS_Rotation2D | PFS_Turbulence | PFS_Orbit;
};

struct TrailParticle
{
	Vector3		m_position;				//!< (0 byte) Current particle position
	Float		m_life;					//!< (12 byte) Particle life
	Float		m_lifeSpanInv;			//!< (16 byte) Relative scale of particle life span ( 0.0f for dead particles )
	Vector3		m_emitterAxis;			//!< (20 byte) Emitter orientation vector from the moment of spawn (forward,right, or up)
	Vector3		m_prevEmitterAxis;		//!< (32 byte) Prev

	Float		m_frame;				//!< (44 byte) Current particle animation frame
	Float		m_baseFrame;			//!< (48 byte) initial particle animation frame
	Vector3		m_color;				//!< (52 byte) Current particle color
	Float		m_alpha;				//!< (64 byte) Current particle alpha
	Vector3		m_baseColor;			//!< (68 byte) Initial particle color
	Float		m_baseAlpha;			//!< (80 byte) Initial particle alpha

	Float		m_eventFrequencyAcum;	//!< (84 byte) Current particle event generation frequency accumulator
	Uint32		m_particleID;			//!< (88 byte)ID for the particle, so it can be associated in the event 

	Vector2		m_size;					//!< (92 byte) Particle stretch along axis
	Vector2		m_baseSize;				//!< (100 byte) Initial particle size

	Float		m_texCoord;				//!< (108 byte) Particle texture coordinate in trail spawn direction
	Float		m_prevTexCoord;			//!< (112 byte) Previous particle texture coordinate in trail spawn direction

	static const Int32 m_fieldSet = PFS_Color | PFS_Size2D | PFS_Frame | PFS_EmitterAxis;
};

struct FacingTrailParticle
{
	Vector3		m_position;				//!< (0 byte) Current particle position
	Float		m_life;					//!< (12 byte) Particle life
	Float		m_lifeSpanInv;			//!< (16 byte) Relative scale of particle life span ( 0.0f for dead particles )
	Vector3		m_velocity;				//!< (20 byte) Current particle velocity
	Vector3		m_baseVelocity;			//!< (32 byte) Initial velocity

	Float		m_frame;				//!< (44 byte) Current particle animation frame
	Float		m_baseFrame;			//!< (48 byte) initial particle animation frame
	Vector3		m_color;				//!< (52 byte) Current particle color
	Float		m_alpha;				//!< (64 byte) Current particle alpha
	Vector3		m_baseColor;			//!< (68 byte) Initial particle color
	Float		m_baseAlpha;			//!< (80 byte) Initial particle alpha

	Float		m_eventFrequencyAcum;	//!< (84 byte) Current particle event generation frequency accumulator
	Uint32		m_particleID;			//!< (88 byte) ID for the particle, so it can be associated in the event

	Vector2		m_size;					//!< (92 byte) Particle stretch along axis
	Vector2		m_baseSize;				//!< (100 byte) Initial particle size

	Float		m_texCoord;				//!< (104 byte) Particle texture coordinate in trail spawn direction
	Float		m_prevTexCoord;			//!< (108 byte) Previous particle texture coordinate in trail spawn direction

	static const Int32 m_fieldSet = PFS_Color | PFS_Size2D | PFS_Frame | PFS_Velocity;
};

struct MeshParticle
{
	Vector3		m_position;			//!< (0 byte) Current particle position
	Float		m_life;				//!< (12 byte) Particle life
	
	Float		m_lifeSpanInv;		//!< (16 byte) Relative scale of particle life span ( 0.0f for dead particles )
	Vector3		m_velocity;			//!< (20 byte) Current particle velocity
	
	Vector3		m_baseVelocity;		//!< (32 byte) Initial velocity
	Float		m_frame;			//!< (44 byte) Current particle animation frame

	Float		m_baseFrame;		//!< (48 byte) initial particle animation frame
	Vector3		m_color;			//!< (52 byte) Current particle color

	Float		m_alpha;			//!< (64 byte) Current particle alpha
	Vector3		m_baseColor;		//!< (68 byte) Initial particle color
	Float		m_baseAlpha;		//!< (80 byte) Initial particle alpha

	Float		m_eventFrequencyAcum;//!< (84 byte) Current particle event generation frequency accumulator
	Uint32		m_particleID;		 //!< (88 byte) ID for the particle, so it can be associated in the event

	Float		m_rotation[4];			//!< (92 byte) Current particle rotation
	Float		m_rotationRate[4];		//!< (108 byte) Current particle rotation speed
	Float		m_baseRotationRate[4];  //!< (124 byte) Initial particle rotation speed

	Vector3		m_size;				 //!< (140 byte) 3D size of the particle
	Vector3		m_baseSize;			 //!< (152 byte) Initial 3D size of the particle

	static const Int32 m_fieldSet = PFS_Velocity | PFS_Size3D | PFS_Frame | PFS_Rotation3D | PFS_Color;
};

struct BeamParticle
{
	Vector3		m_position;				//!< (0 byte) Current particle position
	Float		m_life;					//!< (12 byte) Particle life
	Float		m_lifeSpanInv;			//!< (16 byte) Relative scale of particle life span ( 0.0f for dead particles )
	Vector3		m_velocity;				//!< (20 byte) Current particle velocity
	Vector3		m_baseVelocity;			//!< (32 byte) Initial velocity

	Float		m_frame;				//!< (44 byte) Current particle animation frame
	Float		m_baseFrame;			//!< (48 byte) initial particle animation frame
	Vector3		m_color;				//!< (52 byte) Current particle color
	Float		m_alpha;
	Vector3		m_baseColor;			//!< (68 byte) Initial particle color
	Float		m_baseAlpha;

	Float		m_eventFrequencyAcum;	//!< (84 byte) Current particle event generation frequency accumulator
	Uint32		m_particleID;			//!< (88 byte) ID for the particle, so it can be associated in the event

	Vector2		m_size;					//!< (92 byte) Current particle size
	Vector2		m_baseSize;				//!< (100 byte) Initial particle size

	Vector3		m_beamSpread;			//!< (108 byte) Current particle rotation

	Vector3		m_turbulence;			//!< (120 byte) Current particle turbulence
	Float		m_turbulenceCounter;	//!< (132 byte) Current particle turbulence timeout

	static const Int32 m_fieldSet = PFS_Velocity | PFS_Color | PFS_Size2D | PFS_Frame | PFS_Turbulence;
};
