/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderResourceBlob.h"

RTTI_BEGIN_ABSTRACT_TYPE(IRenderResourceBlob);
RTTI_PARENT_TYPE(ISerializable);
RTTI_END_TYPE();

serialization::LoadingToken IRenderResourceBlob::StartStreaming(io::EAsyncPriority priority)
{
	RED_FATAL("Resource is not streamable");

	job::Counter dummyCounter;
	return serialization::LoadingToken(std::move(dummyCounter));
}

