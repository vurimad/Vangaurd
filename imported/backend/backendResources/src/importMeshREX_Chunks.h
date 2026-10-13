/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include <vector>
#include "re_archive/reFileArchive.h"
#include "importMeshREX_Common.h"

class CMesh;

namespace mesh
{
	class DataViewRO;
	class DataBuilder;
}


ImportResult ImportChunks( CMesh* meshData, const std::vector< ReFileMeshPtr >& meshes, const red::DynArray< red::DynArray< Int32 > >& rigMapping,
						   const mesh::DataViewRO& meshView, const mesh::ChunkFlags& chunkFlags, const Bool importUV2AsMorphVertexData, const Bool prepareTopology,
						   THandle<CREXMeshResourceImportOptions>& options, mesh::DataBuilder& buildData, const Bool importDestructionData );

