/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamUICollisionData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamUICollisionData, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_uvs );
	RTTI_PROPERTY( m_trianglesIndices );
	RTTI_PROPERTY( m_vertices );
RTTI_END_TYPE();

namespace mesh
{
	MeshParamUICollisionData::MeshParamUICollisionData( const red::DynArray< Vector2 >& uvs, const red::DynArray<Uint16>& trianglesIndices , const red::DynArray<Vector3>& vertices )
		: m_uvs( uvs )
		, m_trianglesIndices( trianglesIndices )
		, m_vertices ( vertices )
	{ 
	}

    void MeshParamUICollisionData::OnMeshChanged( const CMesh& mesh ) {}

	const red::DynArray<Vector2>& MeshParamUICollisionData::GetUVs() const
	{
		return m_uvs;
	}

	const red::DynArray<Uint16>& MeshParamUICollisionData::GetTrianglesIndices() const
	{
		return m_trianglesIndices;
	}

	const red::DynArray<Vector3> MeshParamUICollisionData::GetVertices() const
	{
		return m_vertices;
	}

} // mesh
