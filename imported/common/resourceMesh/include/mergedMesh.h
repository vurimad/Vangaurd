/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "mesh.h"

class RESOURCEMESH_API MergedMesh : public CMesh
{
	RTTI_DECLARE_TYPE(MergedMesh);
	IMPLEMENT_RESOURCE_INTERFACE( "", "mergedmesh", "MergedMesh" );
	RED_BASE_CLASS( CMesh);
	RED_USE_MEMORY_POOL( rend::PoolMesh );

public:	
	MergedMesh();

	~MergedMesh() = default;
};
