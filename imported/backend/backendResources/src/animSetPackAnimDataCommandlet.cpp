/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resaveCommandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"

// for debugging purpose.
class AnimSetPackAnimDataCommandlet: public ResaveCommandlet
{
	RTTI_DECLARE_TYPE( AnimSetPackAnimDataCommandlet );
public:
	AnimSetPackAnimDataCommandlet()
		: ResaveCommandlet( RED_NAME_CONSTEXPR( "animSetPackAnimData" ) )
	{}

protected:
	virtual bool OnResourcePreSave( CResource & resource ) override
	{
		const res::ResourcePath& resourcePath = resource.GetPath();
		if( !resource.IsA< anim::AnimSet >() )
		{
			RED_LOG_ERROR( "Resource '%hs' is not an anim set.", resourcePath.ToDebugString() );
			return false;
		}

		{
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( resource.HandleFromThis() );
		}

		anim::AnimSet& animSet = static_cast<anim::AnimSet&>(resource);
		return animSet.COOKING_PackAnimationData();
	}
};


RTTI_BEGIN_TYPE( AnimSetPackAnimDataCommandlet )
RTTI_PARENT_TYPE( ResaveCommandlet )
RTTI_END_TYPE()