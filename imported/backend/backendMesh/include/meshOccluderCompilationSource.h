/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../backend/backendVisibility/include/visOccluderMeshCompilationSource.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"

namespace mesh
{

	class BACKEND_MESH_API VisOccluderMeshCompilationSource : public tools::vis::IOccluderMeshCompilationSource, public red::NonCopyable
	{
	private:
		THandle< CMesh >			m_mesh;
		mesh::DataViewRO			m_meshView;
		mesh::utils::TChunkIndices	m_chunkIndices;

	public:
		VisOccluderMeshCompilationSource( const CMesh& mesh, Uint32 lod );

		virtual String GetMeshName() const override;

		virtual Uint32 GetNumChunks() const override;
		virtual ChunkData GetChunkData( Uint32 chunkIndex ) const override;
		virtual Bool GetChunkTwoSided( Uint32 chunkIndex ) const override;
	};

}
