/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "meshParameter.h"

namespace mesh
{
	// Parameter used by terrain meshes, for now supplies per-chunk bounding boxes, because CMesh does not keep them (only one box per whole mesh).
	class RESOURCEMESH_API MeshParamTerrain : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamTerrain );

	public:
		MeshParamTerrain();
		MeshParamTerrain( const red::DynArray< Box >& chunkBoundingBoxes );
		virtual ~MeshParamTerrain() override;

		MeshParamTerrain& operator=( const MeshParamTerrain& other );

		const red::DynArray< Box >& GetChunkBoundingBoxes() const { return m_chunkBoundingBoxes; }

		void SetChunkBoundingBoxes( const red::DynArray< Box >& chunkBoundingBoxes );

		virtual void OnMeshChanged( const CMesh& mesh ) override final;

	private:
		red::DynArray< Box > m_chunkBoundingBoxes;
	};

} // mesh
