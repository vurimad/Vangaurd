/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "syncedAnimationsMetadata.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( SyncedAnimationData, res );
RTTI_PROPERTY( m_animationName ).readOnly();
RTTI_PROPERTY( m_animsetPath ).readOnly();
RTTI_PROPERTY( m_startFrame ).readOnly();
RTTI_PROPERTY( m_endFrame ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SyncedAnimationsMetadata, res );
RTTI_PARENT_TYPE( IBackendData );
RTTI_PROPERTY( m_syncedAnimations ).readOnly();
RTTI_PROPERTY( m_originTransform ).readOnly();
RTTI_END_TYPE();

namespace res
{

	SyncedAnimationsMetadata::SyncedAnimationsMetadata(){};

	void SyncedAnimationsMetadata::AddSyncedAnimation( const String& name, const String& animsetName, const Int32& start, const Int32& end )
	{
		SyncedAnimationData anim_data;
		anim_data.m_animationName = RED_NAME( name );
		anim_data.m_animsetPath = animsetName;
		anim_data.m_startFrame = start;
		anim_data.m_endFrame = end;
		m_syncedAnimations.PushBack( anim_data );
	}


} // namespace res
