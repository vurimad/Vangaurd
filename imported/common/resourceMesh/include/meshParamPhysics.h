/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace physics
{
	class SystemResource;
}

namespace mesh
{
	struct RESOURCEMESH_API MeshParamPhysics : public MeshParameter
	{
		RTTI_DECLARE_TYPE(MeshParamPhysics);

		MeshParamPhysics();
		~MeshParamPhysics();

		void SetPhysicsData(const THandle< physics::SystemResource >& data);
		RED_FORCE_INLINE const THandle< physics::SystemResource >& GetPhysicsData() const { return m_physicsData; }
		virtual void OnMeshChanged(const CMesh& mesh) override final;

		THandle< physics::SystemResource  >	m_physicsData;
	};

} // mesh
