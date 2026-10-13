#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/renderData/typeRegistryFuncImpl.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// acseparate move these to textureEnums.cpp. right now it's here because of rtti problem with texturegroups.

#include "renderPublicEnums.h"
#include "textureEnums.h" // acseparate remove this after rtti defines will be moved to textureEnums.cpp
#include "particleEmitterEnum.h"
#include "renderingMemory.h"
#include "renderPublicEnums.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM( EDepthCollisionEffect );
RTTI_ENUM_OPTION( DCE_Bounce );
RTTI_ENUM_OPTION( DCE_Glide );
RTTI_ENUM_OPTION( DCE_Kill );
RTTI_END_ENUM();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM(ENoiseType);
RTTI_ENUM_OPTION(NT_Random);
RTTI_ENUM_OPTION(NT_Simplex2D);
RTTI_ENUM_OPTION(NT_Simplex3D);
RTTI_END_ENUM();


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM( EParticleEventType );
RTTI_ENUM_OPTION( PET_Any );
RTTI_ENUM_OPTION( PET_Death );
RTTI_ENUM_OPTION( PET_OverLife );
RTTI_ENUM_OPTION( PET_OverDistance );
RTTI_ENUM_OPTION( PET_Collision );
RTTI_END_ENUM();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM( EParticleEventSpawnObject );
RTTI_ENUM_OPTION( PESO_Particle );
RTTI_ENUM_OPTION( PESO_Decal );
RTTI_END_ENUM();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RTTI_BEGIN_ENUM( EMeshParticleOrientationMode );
RTTI_ENUM_OPTION( MPOM_Normal );
RTTI_ENUM_OPTION( MPOM_MovementDirection );
RTTI_ENUM_OPTION( MPOM_NoRotation );
RTTI_END_ENUM();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RED_MODULE(renderData)
{
	RegisterRenderDataRTTI();	
	rend::InitializeMemoryPools();
}