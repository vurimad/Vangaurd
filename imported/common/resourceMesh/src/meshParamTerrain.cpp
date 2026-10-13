/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamTerrain.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamTerrain, mesh );
	RTTI_PARENT_TYPE( mesh::MeshParameter );
	RTTI_PROPERTY( m_chunkBoundingBoxes );
RTTI_END_TYPE();

namespace mesh
{
	MeshParamTerrain::MeshParamTerrain()
		: m_chunkBoundingBoxes( rend::PoolMesh() )
	{
	}

	MeshParamTerrain::MeshParamTerrain( const red::DynArray< Box >& chunkBoundingBoxes )
		: m_chunkBoundingBoxes( chunkBoundingBoxes )
	{
	}

	MeshParamTerrain::~MeshParamTerrain() = default;

	MeshParamTerrain& MeshParamTerrain::operator=( const MeshParamTerrain& other )
	{
		m_chunkBoundingBoxes = other.m_chunkBoundingBoxes;
		return *this;
	}

	void MeshParamTerrain::OnMeshChanged( const CMesh& mesh )
	{
	}

	void MeshParamTerrain::SetChunkBoundingBoxes( const red::DynArray< Box >& chunkBoundingBoxes )
	{
		m_chunkBoundingBoxes = chunkBoundingBoxes;
	}

} // mesh
