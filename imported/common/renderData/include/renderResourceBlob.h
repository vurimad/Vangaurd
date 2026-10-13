/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redReflection/include/serializationLoadingToken.h"


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum ERenderResourceType 
{
	RRT_Texture,
	RRT_Mesh,
	RRT_MorphTargetMesh,
	RRT_Particle,
	RRT_MultilayerMask,
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace io
{
	enum EAsyncPriority : Uint8;
}

class RENDER_DATA_API IRenderResourceBlob : public ISerializable
{
	RTTI_DECLARE_TYPE( IRenderResourceBlob );
	RED_USE_MEMORY_POOL( rend::PoolRenderBlobs );

public:
	RED_INLINE virtual ERenderResourceType GetType() const = 0;

	virtual Uint32	GetVersion() const = 0;

	virtual Bool	IsVersionCurrent() const = 0;
	virtual Bool	IsVersionValid() const = 0;

	virtual Uint32	GetDataSize() const = 0;

	virtual serialization::LoadingToken StartStreaming(io::EAsyncPriority priority);
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////