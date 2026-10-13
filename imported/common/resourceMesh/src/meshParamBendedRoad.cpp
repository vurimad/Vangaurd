/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshParamBendedRoad.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(MeshParamBendedRoad, mesh);
	RTTI_PARENT_TYPE(mesh::MeshParameter);
	RTTI_PROPERTY( m_occInds );
	RTTI_PROPERTY( m_occVerts );
	RTTI_PROPERTY( m_occSkinWeights );
	RTTI_PROPERTY( m_occSkinInds );
	RTTI_PROPERTY( m_collInds );
	RTTI_PROPERTY( m_collVerts );
	RTTI_PROPERTY( m_collSkinWeights );
	RTTI_PROPERTY( m_collSkinInds );
	RTTI_PROPERTY( m_collMaterialName );
	RTTI_PROPERTY( m_collFilterPresetName );
	RTTI_PROPERTY( m_collFaceMatInds );
	RTTI_PROPERTY( m_collFaceMaterialNames );
RTTI_END_TYPE();

namespace mesh
{

	void MeshParamBendedRoad::OnMeshChanged( const CMesh& mesh ) {}

	MeshParamBendedRoad::MeshParamBendedRoad() :
		m_occInds( red::DynArray< Uint16 > {red::PoolResource()} ),
		m_occVerts( red::DynArray< Vector4 > {red::PoolResource()} ),
		m_occSkinWeights( red::DynArray<Vector4> {red::PoolResource()} ),
		m_occSkinInds( red::DynArray<Color> {red::PoolResource()} ),
		m_collInds( red::DynArray< red::DynArray< Uint16 >>{red::PoolResource()} ),
		m_collVerts( red::DynArray< red::DynArray< Vector3>> {red::PoolResource()} ),
		m_collSkinWeights( red::DynArray< red::DynArray<Vector4>> {red::PoolResource()} ),
		m_collSkinInds( red::DynArray< red::DynArray< Color >> {red::PoolResource()} ),
		m_collMaterialName( red::DynArray< String > {red::PoolResource()} ),
		m_collFilterPresetName( red::DynArray< String > {red::PoolResource()} ),
		m_collFaceMatInds( red::DynArray < red::DynArray< Uint16>>{red::PoolResource()} ),
		m_collFaceMaterialNames( red::DynArray < red::DynArray< String>>{red::PoolResource()} )
	{
		
		
	}
} // mesh
