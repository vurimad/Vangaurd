/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace mesh
{
	class RESOURCEMESH_API MeshParamUICollisionData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamUICollisionData );

	public:
		MeshParamUICollisionData() = default;
		MeshParamUICollisionData( const red::DynArray<Vector2>& uvs, const red::DynArray<Uint16>& trianglesIndices, const red::DynArray< Vector3 >& vertices );

		virtual void OnMeshChanged( const CMesh& mesh ) override final;

		const red::DynArray<Vector2>& GetUVs() const;
		const red::DynArray<Uint16>& GetTrianglesIndices() const;
		const red::DynArray< Vector3 > GetVertices() const;

	private:
		red::DynArray<Vector2> m_uvs{ rend::PoolMesh() };
		red::DynArray<Uint16> m_trianglesIndices{ rend::PoolMesh() };
		red::DynArray< Vector3 > m_vertices{ rend::PoolMesh() };
	};

} // mesh
