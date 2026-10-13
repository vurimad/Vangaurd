/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderObject.h"

//******************************************************************************************************//
//******************************************************************************************************//

/// Base render resource
class RENDER_DATA_API IRenderResource : public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolRenderResources );

protected:
	virtual ~IRenderResource();

public:
	IRenderResource();

	// Get resource category
	virtual CName GetCategory() const=0;

	// Calculate video memory used by resource
	virtual Uint32 GetUsedVideoMemory() const;
};

//******************************************************************************************************//
//******************************************************************************************************//
