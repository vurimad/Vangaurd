/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../renderData/include/renderMaterialSetupParam.h"
#include "../../redContainers/include/map.h"

class IMaterial;


namespace mesh
{
	class DataViewRO;
	class MeshAppearance;
}

// Material setup parameter constructor helper class
class RESOURCEMESH_API CMaterialSetupParamsProvider
{
public:
	CMaterialSetupParamsProvider( const mesh::DataViewRO& meshView, const THandle< IMaterial >& material, Bool preloadResources );
	CMaterialSetupParamsProvider( const mesh::MeshAppearance& appearance, Bool preloadResources );
	~CMaterialSetupParamsProvider();

	RED_INLINE const red::Map< CName, SMaterialSetupEntry >& GetMaterialSetupEntries() const { return m_params; }
	RED_INLINE const red::DynArray< CName >& GetChunkMaterialNames() const { return m_chunkMaterialNames; }

private:
	red::Map< CName, SMaterialSetupEntry > m_params;	// For each material name, parameters for that material
	red::DynArray< CName > m_chunkMaterialNames;		// For each chunk, corresponding material name

	void ConstructMaterialSetupParameters( const THandle<IMaterial>& material, const CName materialName, Bool preloadResources );

	void ConstructChunkMaterialNames( const mesh::DataViewRO& meshView );
	void ConstructChunkMaterialNames( const mesh::MeshAppearance& appearance );
};
