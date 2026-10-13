/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../include/meshParamShadowMeshCreationData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamShadowMeshCreationData, mesh );
RTTI_PARENT_TYPE( mesh::MeshParameter ); 
RTTI_PROPERTY( m_geometries );
RTTI_PROPERTY( m_bonesPerGeometry );
RTTI_END_TYPE();



namespace mesh
{
	MeshParamShadowMeshCreationData::MeshParamShadowMeshCreationData()
	{}

	void MeshParamShadowMeshCreationData::OnMeshChanged( const CMesh& mesh )
	{
	}
} // mesh
