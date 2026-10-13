/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "baseSceneRecorder.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( BaseSceneRecorder, tools )
RTTI_END_TYPE();

namespace tools
{
	BaseSceneRecorder::BaseSceneRecorder()
		: interop::utils::Object( "BaseSceneRecorder" )
	{
	}
}