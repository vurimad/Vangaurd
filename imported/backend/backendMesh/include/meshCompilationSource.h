/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifndef NO_RESOURCE_COOKING

#include "../../../common/renderBackend/include/renderMeshCompilationSource.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"

class CMesh;
enum ECookingPlatform : Uint8;

/*----------------------------------------------------------------------------------------------------------
	CMeshCompilationSource ( CMesh wrapper used during render resource blob compilation )
	TODO: Move to external cooker so it's not part of the engine
----------------------------------------------------------------------------------------------------------*/

class CMeshCompilationSource : public tools::IMeshCompilationSource, public red::NonCopyable
{
public:
	CMeshCompilationSource( const CMesh& mesh, ECookingPlatform platform, Bool verbose );

	virtual red::String GetMeshName() const;

	virtual Uint64 GetResourceHash() const;

	virtual Vector4 GetQuantizationScale() const;

	virtual Vector4 GetQuantizationOffset() const;

	virtual void GetBonePositions( red::DynArray< Vector4 >& outPositions ) const;

	virtual Uint32 GetCustomData( red::DynArray< Uint8 >& outData ) const;

	virtual Uint32 GetTopologyData( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const;
	virtual Uint32 GetTopologyMetadata( Uint32 chunkIndex, red::DynArray< Uint8 >& outData ) const;
	
	virtual Uint32 GetNumMeshLods() const;

	virtual Bool ValidateMeshData() const;

	virtual Bool GetPackingSource( tools::MeshPackingSource* source ) const;

	virtual void GetWindParams( void** windParams ) const;

private:
	const CMesh*		m_mesh;
	mesh::DataViewRO	m_view;
	Bool				m_verbose;
};

#endif