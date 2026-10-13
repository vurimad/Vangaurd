/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace physics
{
	class ICollider;
}

namespace mesh
{
	class RESOURCEMESH_API MeshParamShadowMeshCreationData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamShadowMeshCreationData );

	public:
		MeshParamShadowMeshCreationData();

		virtual void	OnMeshChanged(const CMesh& mesh) override final;

		red::DynArray< THandle<physics::ICollider>> m_geometries{rend::PoolMesh() };
		red::DynArray<Int32> m_bonesPerGeometry{ rend::PoolMesh() };
	};

} // mesh
