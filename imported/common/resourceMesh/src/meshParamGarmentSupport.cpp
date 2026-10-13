/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamGarmentSupport.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamGarmentSupport, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_chunkCapVertices );
	RTTI_PROPERTY( m_customMorph );

RTTI_END_TYPE();

namespace mesh
{

    void MeshParamGarmentSupport::OnMeshChanged( const CMesh& mesh ) {}

	MeshParamGarmentSupport::MeshParamGarmentSupport() :m_chunkCapVertices( red::DynArray < red::DynArray<Uint32>> {red::PoolResource()} )
	{
		
		
	}
} // mesh
