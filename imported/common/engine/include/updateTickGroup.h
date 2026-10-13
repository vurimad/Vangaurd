/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// See dev/src/docs/tick for useful information in migrating to update buckets
enum class UpdateTickGroup : Uint8
{
	FrameBegin,		// Player Update, Streaming Update
	
	Multiplayer_UpdateStateSnapshots,

	EntityUpdateState, // Reserved for Attach, Detach and Dispose of Entity. DO NOT REGISTER HERE.

	PreBuckets,

	Internal_Buckets,

	PostBuckets,
	CameraUpdate,

	PlayerAimUpdate,
	PostPlayerAimUpdate,	// GameEffectSystem; ProjectilePreview; TargetingSystem;

	MappinsUpdate,			// FXSystem; StatusEffectSystem; GlobalTvSystem;

	BlackboardCallbacks_SecondPass,	// because after previous call we have new data in the blackboard and ui system doesn't know about them in that frame
	PreRenderUpdate,		

	Multiplayer_CaptureStateSnapshots,

	//////////////////////////////////////////////////////////////////////////

	COUNT,
};


enum class UpdateBucketPhase : Uint8
{
	EntitiesPreTick,
	EntitiesServiceEvents,

	PrePhysicsTick,
	UpdateTransformPrePhysics,

	PhysicsFlushBufferedState, // StatsSystem

	PhysicsExecuteAsyncQueries, // AnimationSystem

	// Sync results after the physics state change.
	// Keep it simple here!!! Conceptually just post-physics callbacks.
	PostPhysicsSyncResults,

	// Update any transform changes again (e.g., after player character kinematic controller locomotion)
	UpdateTransformPostPhysics,

	AnimationUpdate, // statPools

	PostPhysicsTick, // Blackboard

	EntitiesPostTick,
	EntitiesPostServiceEvents,

	COUNT,
};


static_assert( (Uint32)UpdateTickGroup::COUNT <= 16, "Think about other ways to communicate between your systems vs halting the whole update loop. Yes, E3 etc, but at least realize this isn't sustainable." );
static_assert( (Uint32)UpdateBucketPhase::COUNT <= 13, "Think about other ways to communicate between your systems vs halting the whole update loop. Yes, E3 etc, but at least realize this isn't sustainable.");