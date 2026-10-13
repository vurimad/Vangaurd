/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/serializable.h"
#include "../../renderData/include/renderingMemory.h"
class CMesh;

namespace mesh
{

	// Generalized component extending mesh functionality
	class RESOURCEMESH_API MeshParameter : public ISerializable
	{
		RTTI_DECLARE_TYPE( MeshParameter );

	public:
		
		RED_USE_MEMORY_POOL( rend::PoolMesh );

		virtual ~MeshParameter();

		// Called when associated mesh structure hash changed, for example number of chunks/LODs or geometry content
		// In most of the cases the content of the parameter should be invalidated or recomputed
		virtual void OnMeshChanged(const CMesh& mesh) = 0;

		// Some mesh parameters hold data that is used for recreating renderblobs and are not required in cooked game
		// setting this to true will strip them entirely from mesh data in cook
		virtual Bool ShouldBeStrippedWhenCooking() const { return false; }
	};

} // mesh
