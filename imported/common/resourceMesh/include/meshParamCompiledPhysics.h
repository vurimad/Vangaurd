/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"
#include "../../../common/physics/include/physicsCollection.h"

namespace physics
{
    struct DeferredCollection;
}

namespace mesh
{
	struct RESOURCEMESH_API MeshParamCompiledPhysics : public MeshParameter
	{
		RTTI_DECLARE_TYPE(MeshParamCompiledPhysics);

        MeshParamCompiledPhysics();
        ~MeshParamCompiledPhysics() override;

		virtual void OnMeshChanged(const CMesh& mesh) override final;

        THandle<physics::DeferredCollection> m_collection = nullptr;
	};

} // mesh
