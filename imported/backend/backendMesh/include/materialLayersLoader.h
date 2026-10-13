#pragma once

#include "../../../common/resourceMaterial/include/materialLayerDefinition.h"
#include "../../../common/resourceMesh/include/mesh.h"

namespace MaterialLayerHelpers
{
	struct ChunkLayerDefinition
	{
		Uint16					m_chunkId;					//!< Chunk id number
		red::String				m_maskPath;					//!< Mask tex
		red::String				m_uniqueNormalPath;			//!< Detail normal tex
		TMaterialLayersArray	m_layers;					//!< Layer parameters
	};

	typedef red::DynArray< ChunkLayerDefinition > TMeshLayerDefinitionArray;

	extern BACKEND_MESH_API Bool Load( const red::AbsolutePath& path, TMeshLayerDefinitionArray& outLayers );

	extern BACKEND_MESH_API void Apply( const THandle< CMesh >& meshData, mesh::DataBuilder& builder,
	                                    const TMeshLayerDefinitionArray& layers, Bool replaceExistingMaterials = true);

	extern BACKEND_MESH_API void ApplySingleChunk( red::DynArray<CName>& outMaterialNames, mesh::DataView& meshView,
	                                               const ChunkLayerDefinition& chunkLayerDef );
};
