/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamPhysics.h"
#include "../../physics/include/physicsSystemResource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamPhysics, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY(m_physicsData).readOnly();
RTTI_END_TYPE();

namespace mesh
{
	MeshParamPhysics::MeshParamPhysics() = default;
	MeshParamPhysics::~MeshParamPhysics() = default;

	void MeshParamPhysics::SetPhysicsData(const THandle< physics::SystemResource >& data)
	{
		m_physicsData = data;
	}

	void MeshParamPhysics::OnMeshChanged(const CMesh& mesh)
	{
	}
} // mesh