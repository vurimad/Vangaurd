/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "morphTargetCommon.h"

//////////////////////////////////////////////////////////////////////////

RTTI_BEGIN_ENUM_IN_NAMESPACE( DiffTextureSize, MorphTargets )
{
	RTTI_ENUM_OPTION( TEXTURE_SIZE_1024x1024 );
	RTTI_ENUM_OPTION( TEXTURE_SIZE_512x512 );
	RTTI_ENUM_OPTION( TEXTURE_SIZE_256x256 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( FaceRegion, MorphTargets );
{
	RTTI_ENUM_OPTION( FACE_REGION_EYES );
	RTTI_ENUM_OPTION( FACE_REGION_NOSE );
	RTTI_ENUM_OPTION( FACE_REGION_MOUTH );
	RTTI_ENUM_OPTION( FACE_REGION_JAW );
	RTTI_ENUM_OPTION( FACE_REGION_EARS );
	RTTI_ENUM_OPTION( FACE_REGION_NONE );
}
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( TextureBlendInfo, MorphTargets );
	RTTI_PROPERTY( m_blend ).editable();
	RTTI_PROPERTY( m_diffSize ).editable();
	RTTI_PROPERTY( m_name ).readOnly().notCooked().notSerialized();
RTTI_END_TYPE();

namespace MorphTargets
{
	JointsForFaceRegions::JointsForFaceRegions( const red::memory::Pool& pool )
	{
		m_joints.Resize( m_joints.Capacity(), JointNames{ pool } );
	}

	JointsForFaceRegions::~JointsForFaceRegions() = default;

	void JointsForFaceRegions::Reserve( Uint32 nameCapacity )
	{
		for( auto& names : m_joints )
		{
			names.Reserve( nameCapacity );
		}
	}

}