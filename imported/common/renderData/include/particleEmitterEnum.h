/**
 * Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redReflection/include/rttiEnum.h"

enum EParticleType : Int32
{
	PT_Simple		= 0,
	PT_SimpleGPU	= 1,
	PT_Trail		= 2,
	PT_FacingTrail	= 3,
	PT_Mesh			= 4,
	PT_Beam			= 5,
	PT_FacingBeam	= 6,

	PT_Invalid		= 7
};

RTTI_DECLARE_ENUM( EParticleType );

enum EParticleEventType
{
	PET_Death,
	PET_OverLife,
	PET_OverDistance,
	PET_Collision,
	PET_Any
};

RTTI_DECLARE_ENUM( EParticleEventType );

enum EParticleEventSpawnObject
{
	PESO_Particle,
	PESO_Decal
};

RTTI_DECLARE_ENUM( EParticleEventSpawnObject );

enum ESimulationPhase
{
	ESP_Emit,
	ESP_Update,

	ESP_Max
};

enum EDepthCollisionEffect
{
	DCE_Bounce		= RED_FLAG( 0 ),
	DCE_Glide		= RED_FLAG( 1 ),
	DCE_Kill		= RED_FLAG( 2 )
};

RTTI_DECLARE_ENUM( EDepthCollisionEffect );

enum ENoiseType
{
	NT_Random,
	NT_Simplex2D,
	NT_Simplex3D
};

RTTI_DECLARE_ENUM(ENoiseType);

enum EMeshParticleOrientationMode : Int32
{
	MPOM_Normal,
	MPOM_MovementDirection,
	MPOM_NoRotation
};

RTTI_DECLARE_ENUM( EMeshParticleOrientationMode );

// Keep it in sync with include_dpl.fx
enum EParticleVertexDrawerType : Int32
{
	PVDT_Billboard,			//!< Using Billboard Drawer
	PVDT_Screen,			//!< Using Screen Drawer
	PVDT_EmitterOrientation,//!< Using Emitter Orientation Drawer
	PVDT_SphereAligned,		//!< Using SphereAligned Drawer
	PVDT_VerticalFixed,		//!< Using SphereAligned Drawer with modified vertex factory
	PVDT_MotionBlurred,		//!< Using MotionBlur Drawer
	PVDT_Trail,				//!< Using Trail Drawer
	PVDT_Mesh,				//!< Using Mesh Drawer
	PVDT_FacingTrail,		//!< Using Facing Trail Drawer
	PVDT_Beam,				//!< Using Beam Drawer
	PVDT_FacingBeam,		//!< Using Facing Beam Drawer

	PVDT_Invalid
};

RTTI_DECLARE_ENUM( EParticleVertexDrawerType );

//////////////////////////////////////////////////////////////////////////
// Simulation type
enum EParticleEmitterSimulationType : Int32
{
	PEST_CPU,
	PEST_GPU,

	PEST_Max
};

RTTI_DECLARE_ENUM( EParticleEmitterSimulationType );

//////////////////////////////////////////////////////////////////////////
// Mask flags for event set
enum EParticleEventFlags
{
	PEF_GenerateDeath		= RED_FLAG(0),
	PEF_GenerateOverLife	= RED_FLAG(1),
	PEF_GenerateOverDistance= RED_FLAG(2),
	PEF_GenerateCollision	= RED_FLAG(3),

	PEF_ReceiveDeath		= RED_FLAG(4),
	PEF_ReceiveOverLife		= RED_FLAG(5),
	PEF_ReceiveOverDistance = RED_FLAG(6),
	PEF_ReceiveCollision	= RED_FLAG(7),
};

static Uint32 st_generateEventMask = (PEF_GenerateDeath | PEF_GenerateOverLife | PEF_GenerateOverDistance | PEF_GenerateCollision);
static Uint32 st_receiveEventMask = (PEF_ReceiveDeath | PEF_ReceiveOverLife | PEF_ReceiveOverDistance | PEF_ReceiveCollision);

//////////////////////////////////////////////////////////////////////////
// Mask flags for initializer set
enum EParticleInitializerFlags : Uint64
{
	PIF_Alpha					= RED_FLAG64( 0 ),
	PIF_AlphaRandom				= RED_FLAG64( 1 ),
	PIF_Color					= RED_FLAG64( 2 ),
	PIF_ColorRandom				= RED_FLAG64( 3 ),

	PIF_LifeTime				= RED_FLAG64( 4 ),
	PIF_LifeTimeRandom			= RED_FLAG64( 5 ),

	PIF_Position				= RED_FLAG64( 6 ),
	PIF_PositionRandom			= RED_FLAG64( 7 ),

	PIF_Rotation				= RED_FLAG64( 8 ),
	PIF_RotationRandom			= RED_FLAG64( 9 ),

	PIF_Rotation3D				= RED_FLAG64( 10 ),
	PIF_Rotation3DRandom		= RED_FLAG64( 11 ),

	PIF_RotationRate			= RED_FLAG64( 12 ),
	PIF_RotationRateRandom		= RED_FLAG64( 13 ),

	PIF_RotationRate3D			= RED_FLAG64( 14 ),
	PIF_RotationRate3DRandom	= RED_FLAG64( 15 ),

	PIF_Size					= RED_FLAG64( 16 ),
	PIF_SizeRandom				= RED_FLAG64( 17 ),

	PIF_Size3D					= RED_FLAG64( 18 ),
	PIF_Size3DRandom			= RED_FLAG64( 19 ),

	PIF_SpawnBox				= RED_FLAG64( 20 ),
	PIF_SpawnCircle				= RED_FLAG64( 21 ),
	PIF_SpawnSphere				= RED_FLAG64( 22 ),

	PIF_Velocity				= RED_FLAG64( 23 ),
	PIF_VelocityRandom			= RED_FLAG64( 24 ),

	PIF_VelocityInherit			= RED_FLAG64( 25 ),
	PIF_VelocityInheritRandom	= RED_FLAG64( 26 ),

	PIF_VelocitySpread			= RED_FLAG64( 27 ),

	PIF_AnimationFrame			= RED_FLAG64( 28 ),
	PIF_AnimationFrameRandom	= RED_FLAG64( 29 ),
	
	PIF_CPUCollision			= RED_FLAG64( 30 ),
};

//////////////////////////////////////////////////////////////////////////
// Mask flags for modifier set
enum EParticleModifierFlags : Uint64
{
	PMF_VelocityOverLifeAbsolute	= RED_FLAG64( 0 ),
	PMF_VelocityOverLifeModulate	= RED_FLAG64( 1 ),

	PMF_AccelerationLocal			= RED_FLAG64( 2 ),
	PMF_AccelerationWorld			= RED_FLAG64( 3 ),

	PMF_AlphaOverLifeAbsolute		= RED_FLAG64( 4 ),
	PMF_AlphaOverLifeModulate		= RED_FLAG64( 5 ),

	PMF_ColorOverLifeAbsolute		= RED_FLAG64( 6 ),
	PMF_ColorOverLifeModulate		= RED_FLAG64( 7 ),

	// 2D particles only
	PMF_1ARotationOverLifeAbsolute	= RED_FLAG64( 8 ),
	PMF_1ARotationOverLifeModulate	= RED_FLAG64( 9 ),

	PMF_1ARotRateOverLifeAbsolute	= RED_FLAG64( 10 ),
	PMF_1ARotRateOverLifeModulate	= RED_FLAG64( 11 ),

	PMF_2DSizeOverLifeAbsolute		= RED_FLAG64( 12 ),
	PMF_2DSizeOverLifeModulate		= RED_FLAG64( 13 ),

	// 3D particle only
	PMF_3ARotationOverLifeAbsolute	= RED_FLAG64( 14 ),
	PMF_3ARotationOverLifeModulate	= RED_FLAG64( 15 ),

	PMF_3ARotRateOverLifeAbsolute	= RED_FLAG64( 16 ),

	PMF_3DSizeOverLifeAbsolute		= RED_FLAG64( 18 ),
	PMF_3DSizeOverLifeModulate		= RED_FLAG64( 19 ),

	// Less common modifiers
	PMF_AnimationFrameBySpeed		= RED_FLAG64( 20 ),
	PMF_AnimationFrameByLife		= RED_FLAG64( 21 ),

	PMF_Target						= RED_FLAG64( 22 ),
	PMF_TargetNode					= RED_FLAG64( 23 ),
	PMF_Turbulize					= RED_FLAG64( 24 ),

	PMF_EffectAlpha					= RED_FLAG64( 25 ),
	PMF_Collision					= RED_FLAG64( 26 ),
	PMF_AlphaByDistance				= RED_FLAG64( 27 ),

	PMF_Noise						= RED_FLAG64( 28 ),
	PMF_VectorFieldAttractor		= RED_FLAG64( 29 ),
	PMF_Force						= RED_FLAG64( 30 ),
	PMF_Orbit						= RED_FLAG64( 31 ),
	PMF_Drag						= RED_FLAG64( 32 ),

	PMF_2DSizeByDistance			= RED_FLAG64( 33 ),
	PMF_3DSizeByDistance			= RED_FLAG64( 34 ),
};